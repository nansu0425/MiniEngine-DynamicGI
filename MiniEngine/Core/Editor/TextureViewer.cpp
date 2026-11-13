#include "Core/pch.h"
#include "Core/Editor/TextureViewer.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Display/Display.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Profiling/EngineProfiling.h"
#include "Core/UI/ImGuiLayer.h"

#include "Output/Core/CompiledShaders/TextureViewCS.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace Graphics;

namespace
{
    enum RootParameter { kConstants, kSource, kDisplay, kNumRootParameters };

    struct Channel
    {
        const char* name;
        uint32_t sourceComponents[3];
    };

    const Channel kChannels[] =
    {
        { "RGB", { 0, 1, 2 } },
        { "R", { 0, 0, 0 } },
        { "G", { 1, 1, 1 } },
        { "B", { 2, 2, 2 } },
        { "A", { 3, 3, 3 } },
    };

    constexpr float kMaxExposureValue = 16.0f;
    constexpr float kMaxZoomLevel = 10.0f;

    struct ViewConstants
    {
        float exposureScale;
        uint32_t sourceComponents[3];
        float texelOrigin[2];
        float texelsPerPixel;
    };

    struct RegisteredTexture
    {
        std::string name;
        ColorBuffer* texture;
    };

    std::vector<RegisteredTexture> s_Textures;
    size_t s_Selected = 0;
    bool s_ShowPanel = false;
    int s_Channel = 0;
    float s_ExposureValue = 0.0f;
    bool s_Fit = true;
    float s_PixelsPerTexel = 1.0f;
    ImVec2 s_CenterTexel;
    ImVec2 s_ViewOrigin;
    ImVec2 s_ViewSize;

    RootSignature s_RootSignature;
    ComputePSO s_ViewPSO(L"Texture View");
    ColorBuffer s_Display;

    const char* GetFormatName(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
        default: return "Other";
        }
    }

    void SelectTextureAt(size_t index)
    {
        s_Selected = index;
        s_Fit = true;
    }

    bool IsZoomInRange(float pixelsPerTexel)
    {
        return pixelsPerTexel >= exp2f(-kMaxZoomLevel) && pixelsPerTexel <= exp2f(kMaxZoomLevel);
    }

    void ShowTextureCombo(void)
    {
        if (!ImGui::BeginCombo("Texture", s_Textures[s_Selected].name.c_str()))
            return;

        for (size_t index = 0; index < s_Textures.size(); ++index)
        {
            const bool selected = index == s_Selected;
            if (ImGui::Selectable(s_Textures[index].name.c_str(), selected))
                SelectTextureAt(index);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    void ShowDisplayControls(void)
    {
        const auto getChannelName = [](void*, int index) { return kChannels[index].name; };
        ImGui::Combo("Channel", &s_Channel, getChannelName, nullptr, IM_ARRAYSIZE(kChannels));
        ImGui::SliderFloat("Exposure (EV)", &s_ExposureValue, -kMaxExposureValue, kMaxExposureValue);

        if (ImGui::Button("Fit"))
            s_Fit = true;
        ImGui::SameLine();
        if (s_PixelsPerTexel >= 1.0f)
            ImGui::Text("x%.3g", s_PixelsPerTexel);
        else
            ImGui::Text("x1/%.3g", 1.0f / s_PixelsPerTexel);
    }

    void FitView(const ColorBuffer& texture, const ImVec2& region)
    {
        s_PixelsPerTexel = std::min(region.x / texture.GetWidth(), region.y / texture.GetHeight());
        s_CenterTexel = ImVec2(texture.GetWidth() * 0.5f, texture.GetHeight() * 0.5f);
    }

    ImVec2 GetTexelAt(const ImVec2& pixel, const ImVec2& region)
    {
        return ImVec2(s_CenterTexel.x + (pixel.x - region.x * 0.5f) / s_PixelsPerTexel,
            s_CenterTexel.y + (pixel.y - region.y * 0.5f) / s_PixelsPerTexel);
    }

    void ZoomAndPanByMouse(const ImVec2& regionOrigin, const ImVec2& region)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemActive() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
        {
            s_CenterTexel.x -= io.MouseDelta.x / s_PixelsPerTexel;
            s_CenterTexel.y -= io.MouseDelta.y / s_PixelsPerTexel;
            s_Fit = false;
        }

        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
        {
            const ImVec2 mouse(io.MousePos.x - regionOrigin.x, io.MousePos.y - regionOrigin.y);
            const ImVec2 texelUnderMouse = GetTexelAt(mouse, region);
            s_PixelsPerTexel = std::clamp(s_PixelsPerTexel * exp2f(io.MouseWheel), exp2f(-kMaxZoomLevel), exp2f(kMaxZoomLevel));
            s_CenterTexel = ImVec2(texelUnderMouse.x - (mouse.x - region.x * 0.5f) / s_PixelsPerTexel,
                texelUnderMouse.y - (mouse.y - region.y * 0.5f) / s_PixelsPerTexel);
            s_Fit = false;
        }
    }

    void FitDisplayToScreen(void)
    {
        if (s_Display.GetResource() != nullptr && s_Display.GetWidth() == g_DisplayWidth && s_Display.GetHeight() == g_DisplayHeight)
            return;

        g_CommandManager.IdleGPU();
        s_Display.Create(L"Texture Viewer Display", g_DisplayWidth, g_DisplayHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    ColorBuffer& ConvertForDisplay(ColorBuffer& texture, const ImVec2& region)
    {
        FitDisplayToScreen();

        ComputeContext& context = ComputeContext::Begin(L"Texture Viewer");
        {
            ScopedTimer _prof(L"Texture Viewer", context);

            const Channel& channel = kChannels[s_Channel];
            const ImVec2 texelOrigin = GetTexelAt(ImVec2(0.0f, 0.0f), region);
            const ViewConstants constants = { exp2f(s_ExposureValue),
                { channel.sourceComponents[0], channel.sourceComponents[1], channel.sourceComponents[2] },
                { texelOrigin.x, texelOrigin.y }, 1.0f / s_PixelsPerTexel };
            context.SetRootSignature(s_RootSignature);
            context.SetPipelineState(s_ViewPSO);
            context.TransitionResource(texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.TransitionResource(s_Display, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.SetConstantArray(kConstants, sizeof(constants) / sizeof(uint32_t), &constants);
            context.SetDynamicDescriptor(kSource, 0, texture.GetSRV());
            context.SetDynamicDescriptor(kDisplay, 0, s_Display.GetUAV());
            context.Dispatch2D((size_t)region.x, (size_t)region.y);
        }
        context.Finish();
        return s_Display;
    }

    void ShowView(ColorBuffer& texture)
    {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 origin(std::floor(cursor.x), std::floor(cursor.y));
        ImGui::SetCursorScreenPos(origin);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 region(std::floor(std::min(available.x, (float)g_DisplayWidth)), std::floor(std::min(available.y, (float)g_DisplayHeight)));
        if (region.x < 1.0f || region.y < 1.0f)
            return;

        s_ViewOrigin = origin;
        s_ViewSize = region;
        if (s_Fit)
            FitView(texture, region);

        ImGui::InvisibleButton("##View", region);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ZoomAndPanByMouse(origin, region);

        ColorBuffer& display = ConvertForDisplay(texture, region);
        const ImVec2 uvMax(region.x / display.GetWidth(), region.y / display.GetHeight());
        ImGui::GetWindowDrawList()->AddImage(ImGuiLayer::BindTexture(display), origin, ImVec2(origin.x + region.x, origin.y + region.y),
            ImVec2(0.0f, 0.0f), uvMax);
    }

    void ShowSelectedTexture(void)
    {
        ColorBuffer& texture = *s_Textures[s_Selected].texture;
        if (texture.GetResource() == nullptr)
        {
            ImGui::TextUnformatted("Not created");
            return;
        }

        ImGui::Text("%u x %u, %s", texture.GetWidth(), texture.GetHeight(), GetFormatName(texture.GetFormat()));
        ShowDisplayControls();
        ShowView(texture);
    }

    ControlChannel::Fields DescribeTexture(const ColorBuffer& texture)
    {
        ControlChannel::Fields fields;
        fields.Flag("created", texture.GetResource() != nullptr);
        if (texture.GetResource() != nullptr)
            fields.Integer("width", texture.GetWidth()).Integer("height", texture.GetHeight()).Text("format", GetFormatName(texture.GetFormat()));
        return fields;
    }

    ControlChannel::Fields DescribeState(void)
    {
        ControlChannel::Fields textures;
        for (const RegisteredTexture& registered : s_Textures)
            textures.Object(registered.name, DescribeTexture(*registered.texture));

        ControlChannel::Fields fields;
        if (!s_Textures.empty())
            fields.Text("texture", s_Textures[s_Selected].name);
        return fields.Text("channel", kChannels[s_Channel].name).Float("exposure_value", s_ExposureValue)
            .Flag("fit", s_Fit).Float("pixels_per_texel", s_PixelsPerTexel)
            .Object("center", ControlChannel::Fields().Float("x", s_CenterTexel.x).Float("y", s_CenterTexel.y))
            .Object("view", ControlChannel::Fields().Integer("left", (int64_t)s_ViewOrigin.x).Integer("top", (int64_t)s_ViewOrigin.y)
                .Integer("width", (int64_t)s_ViewSize.x).Integer("height", (int64_t)s_ViewSize.y))
            .Object("textures", textures);
    }

    bool SelectTexture(const std::string& name)
    {
        const auto found = std::find_if(s_Textures.begin(), s_Textures.end(), [&name](const RegisteredTexture& texture) { return texture.name == name; });
        if (found == s_Textures.end())
            return false;
        SelectTextureAt(found - s_Textures.begin());
        return true;
    }

    bool SelectChannel(const std::string& name)
    {
        const auto found = std::find_if(std::begin(kChannels), std::end(kChannels), [&name](const Channel& channel) { return name == channel.name; });
        if (found == std::end(kChannels))
            return false;
        s_Channel = (int)(found - std::begin(kChannels));
        return true;
    }

    bool SetExposureValue(const std::string& text)
    {
        float exposureValue = 0.0f;
        if (!MeasurementConditions::ReadFloat(text, exposureValue) || !(exposureValue >= -kMaxExposureValue && exposureValue <= kMaxExposureValue))
            return false;
        s_ExposureValue = exposureValue;
        return true;
    }

    bool SetZoom(const std::string& text)
    {
        float pixelsPerTexel = 0.0f, centerX = 0.0f, centerY = 0.0f;
        char extra = 0;
        if (sscanf_s(text.c_str(), "%f %f %f %c", &pixelsPerTexel, &centerX, &centerY, &extra, 1) != 3)
            return false;
        if (!IsZoomInRange(pixelsPerTexel) || !std::isfinite(centerX) || !std::isfinite(centerY))
            return false;
        s_PixelsPerTexel = pixelsPerTexel;
        s_CenterTexel = ImVec2(centerX, centerY);
        s_Fit = false;
        return true;
    }

    void ChangeViewByCommand(const std::string& arguments, const ControlChannel::Reply& reply)
    {
        const size_t valueStart = arguments.find(' ');
        const std::string action = arguments.substr(0, valueStart);
        const std::string value = valueStart == std::string::npos ? std::string() : Utility::Trim(arguments.substr(valueStart + 1));

        std::string error;
        if (action == "select")
            error = SelectTexture(value) ? "" : "no texture named '" + value + "'. state texture_viewer.textures has the names";
        else if (action == "channel")
            error = SelectChannel(value) ? "" : "no channel '" + value + "'. The channels are RGB, R, G, B and A";
        else if (action == "ev")
            error = SetExposureValue(value) ? "" : "ev takes a number from -16 to 16, not '" + value + "'";
        else if (action == "zoom")
            error = SetZoom(value) ? "" : "zoom takes pixels per texel from 1/1024 to 1024 and the texel at the center, not '" + value + "'";
        else if (action == "fit" && value.empty())
            s_Fit = true;
        else
            error = "usage: texture-viewer select <name> | channel RGB|R|G|B|A | ev <value> | zoom <pixels per texel> <x> <y> | fit";

        if (!error.empty())
        {
            reply.Error(error);
            return;
        }
        ControlChannel::AfterFrames(1, [reply] { reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("texture_viewer"))); });
    }

    void ShowContent(void)
    {
        if (s_Textures.empty())
        {
            ImGui::TextUnformatted("No textures");
            return;
        }

        ShowTextureCombo();
        ShowSelectedTexture();
    }
}

void TextureViewer::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters, 0);
    s_RootSignature[kConstants].InitAsConstants(0, sizeof(ViewConstants) / sizeof(uint32_t));
    s_RootSignature[kSource].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    s_RootSignature[kDisplay].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature.Finalize(L"Texture View");

    s_ViewPSO.SetRootSignature(s_RootSignature);
    s_ViewPSO.SetComputeShader(g_pTextureViewCS, sizeof(g_pTextureViewCS));
    s_ViewPSO.Finalize();

    ControlChannel::Register("texture-viewer", "texture-viewer select <name> | channel RGB|R|G|B|A | ev <value> | zoom <pixels per texel> <x> <y> "
        "| fit: the texture the Texture Viewer panel shows, the channel it shows, its exposure in EV from -16 to 16 and its zoom, as the panel's "
        "Texture, Channel, Exposure (EV), mouse wheel, drag and Fit change them. A channel of R, G, B or A shows that component as gray. "
        "zoom shows each texel as pixels per texel screen pixels, from 1/1024 to 1024, with texel coordinate (x, y) at the center of the view, "
        "where texel (i, j) covers [i, i + 1) x [j, j + 1). Each screen pixel shows the one texel at its center. fit, and selecting a texture, "
        "fits the whole texture in the view while the panel is open, until zoom, the wheel or a drag. state texture_viewer has them, "
        "the view where the panel last drew the texture in client pixels, and the textures it can show, each with its size and format once "
        "its owner has created it", ChangeViewByCommand);
    ControlChannel::RegisterState("texture_viewer", DescribeState);
}

void TextureViewer::Shutdown(void)
{
    s_Display.Destroy();
}

void TextureViewer::AddTexture(const std::string& name, ColorBuffer& texture)
{
    s_Textures.push_back({ name, &texture });
}

void TextureViewer::Show(void)
{
    ImGuiLayer::ShowPanel("Texture Viewer", s_ShowPanel, ShowContent);
}
