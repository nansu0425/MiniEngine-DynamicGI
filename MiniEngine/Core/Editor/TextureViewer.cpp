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
#include "Core/Graphics/Resource/GpuBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"
#include "Core/Profiling/EngineProfiling.h"
#include "Core/UI/ImGuiLayer.h"

#include "Output/Core/CompiledShaders/ReadTexelCS.h"
#include "Output/Core/CompiledShaders/TextureViewCS.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>
#include <vector>

using namespace Graphics;

namespace
{
    enum RootParameter { kConstants, kSource, kOutput, kNumRootParameters };

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
    constexpr float kMaxSummaryInterval = 10.0f;
    constexpr float kMaxZoomLevel = 10.0f;
    constexpr uint32_t kNumReadbackSlots = 4;

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

    struct TexelCoordinate
    {
        int32_t x;
        int32_t y;
    };

    struct ReadbackSlot
    {
        bool pending;
        uint64_t fence;
        size_t textureIndex;
        TexelCoordinate texel;
    };

    struct InspectedTexel
    {
        size_t textureIndex;
        TexelCoordinate texel;
        float value[4];
    };

    struct TexelSummary
    {
        size_t textureIndex;
        TexelCoordinate texel;
        uint32_t frames;
        double sum[4];
        float min[4];
        float max[4];
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
    std::optional<TexelCoordinate> s_InspectTarget;
    std::optional<InspectedTexel> s_Inspected;
    std::optional<TexelSummary> s_Summarizing;
    double s_SummarizingSince = 0.0;
    std::optional<TexelSummary> s_Summary;
    float s_SummaryInterval = 1.0f;

    RootSignature s_RootSignature;
    ComputePSO s_ViewPSO(L"Texture View");
    ComputePSO s_ReadTexelPSO(L"Texture Viewer Read Texel");
    ColorBuffer s_Display;
    StructuredBuffer s_TexelValue;
    ReadbackBuffer s_TexelReadback;
    ReadbackSlot s_ReadbackSlots[kNumReadbackSlots] = {};
    uint32_t s_NextReadbackSlot = 0;

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
        ImGui::SliderFloat("Value Interval (s)", &s_SummaryInterval, 0.0f, kMaxSummaryInterval, "%.1f");

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

    bool IsInside(const ColorBuffer& texture, const TexelCoordinate& texel)
    {
        return texel.x >= 0 && texel.y >= 0 && (uint32_t)texel.x < texture.GetWidth() && (uint32_t)texel.y < texture.GetHeight();
    }

    TexelCoordinate GetTexelUnderMouse(const ImVec2& regionOrigin, const ImVec2& region)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 pixelCenter(std::floor(mouse.x - regionOrigin.x) + 0.5f, std::floor(mouse.y - regionOrigin.y) + 0.5f);
        const ImVec2 texel = GetTexelAt(pixelCenter, region);
        return { (int32_t)std::floor(texel.x), (int32_t)std::floor(texel.y) };
    }

    TexelSummary StartSummary(size_t textureIndex, const TexelCoordinate& texel)
    {
        TexelSummary summary = { textureIndex, texel, 0, {}, {}, {} };
        std::fill(std::begin(summary.min), std::end(summary.min), FLT_MAX);
        std::fill(std::begin(summary.max), std::end(summary.max), -FLT_MAX);
        return summary;
    }

    void AddToSummary(TexelSummary& summary, const float (&value)[4])
    {
        ++summary.frames;
        for (int channel = 0; channel < 4; ++channel)
        {
            summary.sum[channel] += value[channel];
            summary.min[channel] = std::min(summary.min[channel], value[channel]);
            summary.max[channel] = std::max(summary.max[channel], value[channel]);
        }
    }

    void Summarize(const InspectedTexel& inspected)
    {
        const double now = ImGui::GetTime();
        const bool sameTexel = s_Summarizing && s_Summarizing->textureIndex == inspected.textureIndex &&
            s_Summarizing->texel.x == inspected.texel.x && s_Summarizing->texel.y == inspected.texel.y;
        if (!sameTexel)
        {
            s_Summarizing = StartSummary(inspected.textureIndex, inspected.texel);
            s_SummarizingSince = now;
            AddToSummary(*s_Summarizing, inspected.value);
            s_Summary = s_Summarizing;
            return;
        }

        AddToSummary(*s_Summarizing, inspected.value);
        if (now - s_SummarizingSince < s_SummaryInterval)
            return;
        s_Summary = s_Summarizing;
        s_Summarizing = StartSummary(inspected.textureIndex, inspected.texel);
        s_SummarizingSince = now;
    }

    void CollectFinishedReadback(void)
    {
        std::vector<uint32_t> finished;
        for (uint32_t index = 0; index < kNumReadbackSlots; ++index)
        {
            ReadbackSlot& slot = s_ReadbackSlots[index];
            if (slot.pending && g_CommandManager.IsFenceComplete(slot.fence))
                finished.push_back(index);
        }
        if (finished.empty())
            return;
        std::sort(finished.begin(), finished.end(), [](uint32_t a, uint32_t b) { return s_ReadbackSlots[a].fence < s_ReadbackSlots[b].fence; });

        const float* values = (const float*)s_TexelReadback.Map();
        for (uint32_t index : finished)
        {
            ReadbackSlot& slot = s_ReadbackSlots[index];
            slot.pending = false;
            InspectedTexel inspected = { slot.textureIndex, slot.texel, {} };
            std::copy(values + index * 4, values + index * 4 + 4, inspected.value);
            s_Inspected = inspected;
            Summarize(inspected);
        }
        s_TexelReadback.Unmap();
    }

    ReadbackSlot* RecordTexelReadback(ComputeContext& context, ColorBuffer& texture)
    {
        if (!s_InspectTarget || !IsInside(texture, *s_InspectTarget))
            return nullptr;
        const uint32_t index = s_NextReadbackSlot;
        ReadbackSlot& slot = s_ReadbackSlots[index];
        if (slot.pending)
            return nullptr;

        context.SetPipelineState(s_ReadTexelPSO);
        context.TransitionResource(s_TexelValue, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.SetConstants(kConstants, s_InspectTarget->x, s_InspectTarget->y);
        context.SetDynamicDescriptor(kSource, 0, texture.GetSRV());
        context.SetDynamicDescriptor(kOutput, 0, s_TexelValue.GetUAV());
        context.Dispatch(1, 1, 1);
        context.TransitionResource(s_TexelValue, D3D12_RESOURCE_STATE_COPY_SOURCE);
        context.CopyBufferRegion(s_TexelReadback, index * sizeof(InspectedTexel::value), s_TexelValue, 0, sizeof(InspectedTexel::value));

        slot = { true, 0, s_Selected, *s_InspectTarget };
        s_NextReadbackSlot = (index + 1) % kNumReadbackSlots;
        return &slot;
    }

    ColorBuffer& RenderView(ColorBuffer& texture, const ImVec2& region)
    {
        FitDisplayToScreen();

        ComputeContext& context = ComputeContext::Begin(L"Texture Viewer");
        ReadbackSlot* readback = nullptr;
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
            context.SetDynamicDescriptor(kOutput, 0, s_Display.GetUAV());
            context.Dispatch2D((size_t)region.x, (size_t)region.y);

            readback = RecordTexelReadback(context, texture);
        }
        const uint64_t fence = context.Finish();
        if (readback != nullptr)
            readback->fence = fence;
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
        if (ImGui::IsItemHovered())
            s_InspectTarget = GetTexelUnderMouse(origin, region);

        ColorBuffer& display = RenderView(texture, region);
        const ImVec2 uvMax(region.x / display.GetWidth(), region.y / display.GetHeight());
        ImGui::GetWindowDrawList()->AddImage(ImGuiLayer::BindTexture(display), origin, ImVec2(origin.x + region.x, origin.y + region.y),
            ImVec2(0.0f, 0.0f), uvMax);
    }

    void ShowInspectedTexel(const ColorBuffer& texture)
    {
        if (!s_Summary || s_Summary->textureIndex != s_Selected || !s_InspectTarget || !IsInside(texture, *s_InspectTarget))
        {
            ImGui::TextDisabled("texel -");
            ImGui::TextDisabled("mean -");
            ImGui::TextDisabled("range -");
            return;
        }

        const TexelSummary& summary = *s_Summary;
        float mean[4];
        for (int channel = 0; channel < 4; ++channel)
            mean[channel] = (float)(summary.sum[channel] / summary.frames);
        ImGui::Text("texel (%d, %d), %u frames", summary.texel.x, summary.texel.y, summary.frames);
        ImGui::Text("mean   r %.4g  g %.4g  b %.4g  a %.4g", mean[0], mean[1], mean[2], mean[3]);
        ImGui::Text("range  r %.4g ~ %.4g  g %.4g ~ %.4g  b %.4g ~ %.4g  a %.4g ~ %.4g", summary.min[0], summary.max[0],
            summary.min[1], summary.max[1], summary.min[2], summary.max[2], summary.min[3], summary.max[3]);
    }

    ControlChannel::Fields DescribeChannels(const float (&value)[4])
    {
        return ControlChannel::Fields().Float("r", value[0]).Float("g", value[1]).Float("b", value[2]).Float("a", value[3]);
    }

    ControlChannel::Fields DescribeSummary(const TexelSummary& summary)
    {
        float mean[4];
        for (int channel = 0; channel < 4; ++channel)
            mean[channel] = (float)(summary.sum[channel] / summary.frames);
        return ControlChannel::Fields().Text("texture", s_Textures[summary.textureIndex].name)
            .Integer("x", summary.texel.x).Integer("y", summary.texel.y).Integer("frames", summary.frames)
            .Object("mean", DescribeChannels(mean)).Object("min", DescribeChannels(summary.min)).Object("max", DescribeChannels(summary.max));
    }

    void ShowSelectedTexture(void)
    {
        CollectFinishedReadback();

        ColorBuffer& texture = *s_Textures[s_Selected].texture;
        if (texture.GetResource() == nullptr)
        {
            ImGui::TextUnformatted("Not created");
            return;
        }

        ImGui::Text("%u x %u, %s", texture.GetWidth(), texture.GetHeight(), GetFormatName(texture.GetFormat()));
        ShowDisplayControls();
        ShowInspectedTexel(texture);
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
        fields.Text("channel", kChannels[s_Channel].name).Float("exposure_value", s_ExposureValue).Float("value_interval", s_SummaryInterval)
            .Flag("fit", s_Fit).Float("pixels_per_texel", s_PixelsPerTexel)
            .Object("center", ControlChannel::Fields().Float("x", s_CenterTexel.x).Float("y", s_CenterTexel.y))
            .Object("view", ControlChannel::Fields().Integer("left", (int64_t)s_ViewOrigin.x).Integer("top", (int64_t)s_ViewOrigin.y)
                .Integer("width", (int64_t)s_ViewSize.x).Integer("height", (int64_t)s_ViewSize.y))
            .Object("textures", textures);
        if (s_Inspected)
        {
            fields.Object("inspected", ControlChannel::Fields().Text("texture", s_Textures[s_Inspected->textureIndex].name)
                .Integer("x", s_Inspected->texel.x).Integer("y", s_Inspected->texel.y).Append(DescribeChannels(s_Inspected->value)));
        }
        if (s_Summary)
            fields.Object("inspected_summary", DescribeSummary(*s_Summary));
        return fields;
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

    bool SetSummaryInterval(const std::string& text)
    {
        float interval = 0.0f;
        if (!MeasurementConditions::ReadFloat(text, interval) || !(interval >= 0.0f && interval <= kMaxSummaryInterval))
            return false;
        s_SummaryInterval = interval;
        return true;
    }

    bool SetInspectTarget(const std::string& text)
    {
        TexelCoordinate texel = {};
        char extra = 0;
        if (sscanf_s(text.c_str(), "%d %d %c", &texel.x, &texel.y, &extra, 1) != 2)
            return false;
        s_InspectTarget = texel;
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
        else if (action == "inspect")
            error = SetInspectTarget(value) ? "" : "inspect takes the texel coordinates as two integers, not '" + value + "'";
        else if (action == "interval")
            error = SetSummaryInterval(value) ? "" : "interval takes seconds from 0 to 10, not '" + value + "'";
        else
            error = "usage: texture-viewer select <name> | channel RGB|R|G|B|A | ev <value> | zoom <pixels per texel> <x> <y> | fit | inspect <x> <y> "
                "| interval <seconds>";

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
    s_RootSignature[kOutput].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature.Finalize(L"Texture View");

    s_ViewPSO.SetRootSignature(s_RootSignature);
    s_ViewPSO.SetComputeShader(g_pTextureViewCS, sizeof(g_pTextureViewCS));
    s_ViewPSO.Finalize();

    s_ReadTexelPSO.SetRootSignature(s_RootSignature);
    s_ReadTexelPSO.SetComputeShader(g_pReadTexelCS, sizeof(g_pReadTexelCS));
    s_ReadTexelPSO.Finalize();

    s_TexelValue.Create(L"Texture Viewer Texel Value", 1, sizeof(InspectedTexel::value));
    s_TexelReadback.Create(L"Texture Viewer Texel Readback", kNumReadbackSlots, sizeof(InspectedTexel::value));

    ControlChannel::Register("texture-viewer", "texture-viewer select <name> | channel RGB|R|G|B|A | ev <value> | zoom <pixels per texel> <x> <y> "
        "| fit | inspect <x> <y> | interval <seconds>: the texture the Texture Viewer panel shows, the channel it shows, its exposure in EV "
        "from -16 to 16, its zoom and the seconds from 0 to 10 over which it summarizes a texel's values, as the panel's Texture, Channel, "
        "Exposure (EV), mouse wheel, drag, Fit and Value Interval (s) change them. A channel of R, G, B or A shows that component as gray. "
        "zoom shows each texel as pixels per texel screen pixels, from 1/1024 to 1024, with texel coordinate (x, y) at the center of the view, "
        "where texel (i, j) covers [i, i + 1) x [j, j + 1). Each screen pixel shows the one texel at its center. fit, and selecting a texture, "
        "fits the whole texture in the view while the panel is open, until zoom, the wheel or a drag. inspect reads texel (x, y) of the texture "
        "every frame while the panel is open, as hovering the view does, without waiting for the GPU. state texture_viewer has them, "
        "the view where the panel last drew the texture in client pixels, and the textures it can show, each with its size and format once "
        "its owner has created it. Its inspected has the float4 of the last texel read with the texture and texel it came from, a few frames "
        "behind. Its inspected_summary has what the panel shows: the mean, min and max of the values read from one texel over the last "
        "interval, and how many frames they came from. An interval of 0 shows every value. A new texel shows its first value at once "
        "and starts the interval over", ChangeViewByCommand);
    ControlChannel::RegisterState("texture_viewer", DescribeState);
}

void TextureViewer::Shutdown(void)
{
    s_Display.Destroy();
    s_TexelValue.Destroy();
    s_TexelReadback.Destroy();
}

void TextureViewer::AddTexture(const std::string& name, ColorBuffer& texture)
{
    s_Textures.push_back({ name, &texture });
}

void TextureViewer::Show(void)
{
    ImGuiLayer::ShowPanel("Texture Viewer", s_ShowPanel, ShowContent);
}
