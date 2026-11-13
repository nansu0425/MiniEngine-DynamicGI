#include "Core/pch.h"
#include "Core/Editor/TextureViewer.h"
#include "Core/Automation/ControlChannel.h"
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

    struct ViewConstants
    {
        float exposureScale;
        uint32_t sourceComponents[3];
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

    void ShowTextureCombo(void)
    {
        if (!ImGui::BeginCombo("Texture", s_Textures[s_Selected].name.c_str()))
            return;

        for (size_t index = 0; index < s_Textures.size(); ++index)
        {
            const bool selected = index == s_Selected;
            if (ImGui::Selectable(s_Textures[index].name.c_str(), selected))
                s_Selected = index;
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
    }

    void FitDisplayTo(const ColorBuffer& texture)
    {
        if (s_Display.GetResource() != nullptr && s_Display.GetWidth() == texture.GetWidth() && s_Display.GetHeight() == texture.GetHeight())
            return;

        g_CommandManager.IdleGPU();
        s_Display.Create(L"Texture Viewer Display", texture.GetWidth(), texture.GetHeight(), 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    ColorBuffer& ConvertForDisplay(ColorBuffer& texture)
    {
        FitDisplayTo(texture);

        ComputeContext& context = ComputeContext::Begin(L"Texture Viewer");
        {
            ScopedTimer _prof(L"Texture Viewer", context);

            const Channel& channel = kChannels[s_Channel];
            const ViewConstants constants = { exp2f(s_ExposureValue),
                { channel.sourceComponents[0], channel.sourceComponents[1], channel.sourceComponents[2] } };
            context.SetRootSignature(s_RootSignature);
            context.SetPipelineState(s_ViewPSO);
            context.TransitionResource(texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.TransitionResource(s_Display, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.SetConstantArray(kConstants, sizeof(constants) / sizeof(uint32_t), &constants);
            context.SetDynamicDescriptor(kSource, 0, texture.GetSRV());
            context.SetDynamicDescriptor(kDisplay, 0, s_Display.GetUAV());
            context.Dispatch2D(s_Display.GetWidth(), s_Display.GetHeight());
        }
        context.Finish();
        return s_Display;
    }

    ImVec2 FitToContentRegion(uint32_t width, uint32_t height)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        float scale = available.x / width;
        if (available.y > 0.0f)
            scale = std::min(scale, available.y / height);
        return ImVec2(width * scale, height * scale);
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
        ImGui::Image(ImGuiLayer::BindTexture(ConvertForDisplay(texture)), FitToContentRegion(texture.GetWidth(), texture.GetHeight()));
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
        return fields.Text("channel", kChannels[s_Channel].name).Float("exposure_value", s_ExposureValue).Object("textures", textures);
    }

    bool SelectTexture(const std::string& name)
    {
        const auto found = std::find_if(s_Textures.begin(), s_Textures.end(), [&name](const RegisteredTexture& texture) { return texture.name == name; });
        if (found == s_Textures.end())
            return false;
        s_Selected = found - s_Textures.begin();
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
        else
            error = "usage: texture-viewer select <name> | channel RGB|R|G|B|A | ev <value>";

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

    ControlChannel::Register("texture-viewer", "texture-viewer select <name> | channel RGB|R|G|B|A | ev <value>: the texture the Texture Viewer "
        "panel shows, the channel it shows and its exposure in EV from -16 to 16, as the panel's Texture, Channel and Exposure (EV) change them. "
        "A channel of R, G, B or A shows that component as gray. state texture_viewer has them and the textures it can show, each with "
        "its size and format once its owner has created it", ChangeViewByCommand);
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
