#include "Model/Raytracing/RayQueryValidation.h"
#include "Model/Raytracing/CameraRays.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Camera/Camera.h"
#include "Core/Display/Display.h"
#include "Core/Effects/TemporalEffects/TemporalEffects.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Descriptor/DescriptorHeap.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"

#include "Output/Model/CompiledShaders/RayQueryValidationCS.h"

#include <imgui.h>
#include <cmath>

using namespace Math;
using namespace Graphics;

namespace
{
    struct MetricDesc
    {
        const char* label;
        // heatmap이 처음 칠하는 log2 범위
        int minExponent;
        int maxExponent;
    };

    struct ModeDesc
    {
        const char* label;
        std::vector<const char*> categories;
        std::vector<MetricDesc> metrics;
    };

    // category와 metric의 순서는 RayQueryValidationCS.hlsl에서 mode마다 정한 순서와 같다.
    const ModeDesc kModes[] =
    {
        // Hit T는 Capture 전에도 값이 범위 밖으로 나가지 않게 ModelViewer camera의 near 1부터 far 10000까지를 칠한다.
        { "Ray Hit", { "Hit", "Miss" }, { { "Hit T", 0, 14 } } },
    };
    static_assert(_countof(kModes) == RayQueryValidation::kNumModes, "mode 개수가 Mode와 다르다");

    // RayQueryValidationStats.hlsli의 상수와 같은 값이다.
    constexpr uint32_t kMaxCategories = 32;
    constexpr uint32_t kMaxMetrics = 4;
    constexpr int kMinExponent = -32;
    constexpr uint32_t kNumOctaves = 64;
    constexpr uint32_t kBinsPerOctaveLog2 = 2;
    constexpr uint32_t kNumRangeBins = kNumOctaves << kBinsPerOctaveLog2;
    constexpr uint32_t kZeroBin = 0;
    constexpr uint32_t kBelowRangeBin = 1;
    constexpr uint32_t kFirstRangeBin = 2;
    constexpr uint32_t kAboveRangeBin = kFirstRangeBin + kNumRangeBins;
    constexpr uint32_t kNonFiniteBin = kAboveRangeBin + 1;
    constexpr uint32_t kNumBins = kNonFiniteBin + 1;
    static_assert(kMinExponent == RayQueryValidation::kMinHeatmapExponent && kMinExponent + (int)kNumOctaves == RayQueryValidation::kMaxHeatmapExponent,
        "heatmap 범위가 histogram 범위와 다르다");

    // RayQueryValidationStats.hlsli와 같은 layout이다.
    struct Stats
    {
        uint32_t pixelCount;
        uint32_t categoryCounts[kMaxCategories];
        struct Metric
        {
            uint32_t maxBits;
            uint32_t bins[kNumBins];
        } metrics[kMaxMetrics];
    };

    // Render가 검증한 frame. Capture한 수치가 어떤 조건에서 나왔는지 함께 보여 준다.
    struct ValidatedFrame
    {
        uint64_t frameCount;
        RayQueryValidation::Mode mode;
        uint32_t width;
        uint32_t height;
        float jitterX;
        float jitterY;
        bool taa;
    };

    struct Capture
    {
        ValidatedFrame frame;
        Stats stats;
    };

    enum RootParameter { kConstants, kScene, kOutputs = kScene + RaytracingScene::kNumRootParameters, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Ray Query Validation");

    ByteAddressBuffer s_Stats;
    ReadbackBuffer s_StatsReadback;
    // RaytracingScene::Bind가 scene descriptor heap을 bind하므로 output과 stats의 UAV도 그 heap에 이 순서로 둔다.
    DescriptorHandle s_UAVs;
    uint32_t s_OutputVersionID = UINT32_MAX;

    bool s_HasValidatedFrame = false;
    ValidatedFrame s_LastFrame;
    bool s_HasCapture = false;
    Capture s_Capture;

    __declspec(align(16)) struct Constants
    {
        CameraRays Rays;
        uint32_t TargetSize[2];
        uint32_t Mode;
        uint32_t NumCategories;
        uint32_t NumMetrics;
        uint32_t HeatmapItem;
        float HeatmapMinLog2;
        float HeatmapMaxLog2;
    };

    // target은 GPU가 멈춘 동안에만 다시 만들어지므로, 그때 heap의 descriptor를 바로 덮어써도 된다.
    void UpdateOutputUAV(const ColorBuffer& target)
    {
        if (s_OutputVersionID == target.GetVersionID())
            return;

        g_Device->CopyDescriptorsSimple(1, s_UAVs, target.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        s_OutputVersionID = target.GetVersionID();
    }

    // stats는 Render가 다음에 지울 때까지 바로 앞 frame의 집계를 그대로 갖는다.
    bool WasLastFrameValidated(void)
    {
        return s_HasValidatedFrame && s_LastFrame.frameCount + 1 == GetFrameCount();
    }

    // 버튼을 누를 때만 읽으므로 GPU를 기다려 바로 읽는다.
    void CaptureLastFrame(void)
    {
        ComputeContext& context = ComputeContext::Begin(L"Ray Query Validation Capture");
        context.CopyBuffer(s_StatsReadback, s_Stats);
        context.Finish(true);

        s_Capture.frame = s_LastFrame;
        std::memcpy(&s_Capture.stats, s_StatsReadback.Map(), sizeof(Stats));
        s_StatsReadback.Unmap();
        s_HasCapture = true;
    }

    float GetRangeBinLowerBound(uint32_t rangeBin)
    {
        const uint32_t bits = (rangeBin + ((127 + kMinExponent) << kBinsPerOctaveLog2)) << (23 - kBinsPerOctaveLog2);
        float bound;
        std::memcpy(&bound, &bits, sizeof(bound));
        return bound;
    }

    std::string DescribeBin(uint32_t bin)
    {
        char text[64];
        if (bin == kZeroBin)
            return "0";
        if (bin == kBelowRangeBin)
            sprintf_s(text, "< %.3g", GetRangeBinLowerBound(0));
        else if (bin == kAboveRangeBin)
            sprintf_s(text, ">= %.3g", GetRangeBinLowerBound(kNumRangeBins));
        else if (bin == kNonFiniteBin)
            return "NaN/Inf";
        else
            sprintf_s(text, "%.3g ~ %.3g", GetRangeBinLowerBound(bin - kFirstRangeBin), GetRangeBinLowerBound(bin - kFirstRangeBin + 1));
        return text;
    }

    // bin에 든 값의 아래 끝 이하인 가장 큰 2의 거듭제곱의 지수
    int GetFloorExponent(uint32_t bin)
    {
        if (bin <= kBelowRangeBin)
            return RayQueryValidation::kMinHeatmapExponent;
        if (bin >= kAboveRangeBin)
            return RayQueryValidation::kMaxHeatmapExponent;
        return kMinExponent + (int)((bin - kFirstRangeBin) >> kBinsPerOctaveLog2);
    }

    // bin에 든 값의 위 끝 이상인 가장 작은 2의 거듭제곱의 지수
    int GetCeilExponent(uint32_t bin)
    {
        if (bin <= kBelowRangeBin)
            return RayQueryValidation::kMinHeatmapExponent;
        if (bin >= kAboveRangeBin)
            return RayQueryValidation::kMaxHeatmapExponent;
        const uint32_t upperRangeBin = bin - kFirstRangeBin + 1;
        return kMinExponent + (int)((upperRangeBin + (1 << kBinsPerOctaveLog2) - 1) >> kBinsPerOctaveLog2);
    }

    uint64_t CountSamples(const Stats::Metric& metric)
    {
        uint64_t samples = 0;
        for (uint32_t bin = 0; bin < kNumBins; ++bin)
            samples += metric.bins[bin];
        return samples;
    }

    // sample을 작은 값부터 세어 fraction만큼에 처음 닿는 bin. NaN과 Inf는 가장 큰 값으로 센다.
    uint32_t FindPercentileBin(const Stats::Metric& metric, uint64_t samples, double fraction)
    {
        const uint64_t rank = (uint64_t)std::ceil(fraction * samples);
        uint64_t cumulative = 0;
        for (uint32_t bin = 0; bin < kNumBins; ++bin)
        {
            cumulative += metric.bins[bin];
            if (cumulative >= rank)
                return bin;
        }
        return kNonFiniteBin;
    }

    void ShowCategoryTable(const ModeDesc& mode, const Stats& stats)
    {
        if (!ImGui::BeginTable("Categories", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
            return;

        ImGui::TableSetupColumn("Category");
        ImGui::TableSetupColumn("Count");
        ImGui::TableSetupColumn("Ratio");
        ImGui::TableHeadersRow();
        for (size_t category = 0; category < mode.categories.size(); ++category)
        {
            const uint32_t count = stats.categoryCounts[category];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mode.categories[category]);
            ImGui::TableNextColumn();
            ImGui::Text("%u", count);
            ImGui::TableNextColumn();
            ImGui::Text("%.4f%%", stats.pixelCount != 0 ? 100.0 * count / stats.pixelCount : 0.0);
        }
        ImGui::EndTable();
    }

    // p50과 p99는 histogram의 bin으로만 알 수 있으므로 그 bin의 범위로 보여 준다.
    void ShowMetricTable(const ModeDesc& mode, const Stats& stats)
    {
        if (mode.metrics.empty() || !ImGui::BeginTable("Metrics", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
            return;

        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Samples");
        ImGui::TableSetupColumn("p50");
        ImGui::TableSetupColumn("p99");
        ImGui::TableSetupColumn("Max");
        ImGui::TableHeadersRow();
        for (size_t index = 0; index < mode.metrics.size(); ++index)
        {
            const Stats::Metric& metric = stats.metrics[index];
            const uint64_t samples = CountSamples(metric);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mode.metrics[index].label);
            ImGui::TableNextColumn();
            ImGui::Text("%llu", samples);
            if (samples == 0)
                continue;

            float max;
            std::memcpy(&max, &metric.maxBits, sizeof(max));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(DescribeBin(FindPercentileBin(metric, samples, 0.5)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(DescribeBin(FindPercentileBin(metric, samples, 0.99)).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.4g", max);
        }
        ImGui::EndTable();
    }

    void ShowCapture(const Capture& capture)
    {
        const ValidatedFrame& frame = capture.frame;
        const ModeDesc& mode = kModes[frame.mode];
        ImGui::Text("%s, %u x %u, TAA %s, jitter (%.3f, %.3f)", mode.label, frame.width, frame.height,
            frame.taa ? "on" : "off", frame.jitterX, frame.jitterY);
        // 모든 pixel을 한 번씩 셌는지 해상도와 견줘 볼 수 있게 한다.
        ImGui::Text("Pixels: %u of %u", capture.stats.pixelCount, frame.width * frame.height);
        ShowCategoryTable(mode, capture.stats);
        ShowMetricTable(mode, capture.stats);
    }
}

const char* RayQueryValidation::GetModeLabel(Mode mode)
{
    return kModes[mode].label;
}

int RayQueryValidation::GetNumHeatmapItems(Mode mode)
{
    return (int)(kModes[mode].categories.size() + kModes[mode].metrics.size());
}

const char* RayQueryValidation::GetHeatmapItemLabel(Mode mode, int item)
{
    const ModeDesc& desc = kModes[mode];
    if (IsMetricItem(mode, item))
        return desc.metrics[item - desc.categories.size()].label;
    return desc.categories[item];
}

bool RayQueryValidation::IsMetricItem(Mode mode, int item)
{
    return item >= (int)kModes[mode].categories.size();
}

RayQueryValidation::Heatmap RayQueryValidation::GetDefaultHeatmap(Mode mode, int item)
{
    if (!IsMetricItem(mode, item))
        return { item, 0, 1 };

    const MetricDesc& metric = kModes[mode].metrics[item - kModes[mode].categories.size()];
    return { item, metric.minExponent, metric.maxExponent };
}

bool RayQueryValidation::FitHeatmapToCapture(Mode mode, Heatmap& heatmap)
{
    if (!s_HasCapture || s_Capture.frame.mode != mode || !IsMetricItem(mode, heatmap.item))
        return false;

    const Stats::Metric& metric = s_Capture.stats.metrics[heatmap.item - kModes[mode].categories.size()];
    const uint64_t samples = CountSamples(metric);
    if (samples == 0)
        return false;

    heatmap.minExponent = GetFloorExponent(FindPercentileBin(metric, samples, 0.01));
    heatmap.maxExponent = std::max(GetCeilExponent(FindPercentileBin(metric, samples, 0.99)), heatmap.minExponent + 1);
    return true;
}

void RayQueryValidation::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_RootSignature, kScene);
    s_RootSignature[kOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
    s_RootSignature.Finalize(L"Ray Query Validation");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pRayQueryValidationCS, sizeof(g_pRayQueryValidationCS));
    s_PSO.Finalize();

    s_Stats.Create(L"Ray Query Validation Stats", sizeof(Stats) / 4, 4);
    s_StatsReadback.Create(L"Ray Query Validation Stats Readback", sizeof(Stats) / 4, 4);

    s_UAVs = Renderer::s_TextureHeap.Alloc(2);
    g_Device->CopyDescriptorsSimple(1, s_UAVs + Renderer::s_TextureHeap.GetDescriptorSize(), s_Stats.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void RayQueryValidation::Shutdown(void)
{
    s_Stats.Destroy();
    s_StatsReadback.Destroy();
}

void RayQueryValidation::ShowSection(void)
{
    const bool lastFrameValidated = WasLastFrameValidated();
    ImGui::BeginDisabled(!lastFrameValidated);
    if (ImGui::Button("Capture"))
        CaptureLastFrame();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(lastFrameValidated ? "Reads the stats of the last frame." : "Pick a Validation view in the View section.");

    if (s_HasCapture)
        ShowCapture(s_Capture);
}

void RayQueryValidation::Render(ComputeContext& context, Mode mode, const Heatmap& heatmap, const Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, ColorBuffer& target)
{
    ScopedTimer _prof(L"Ray Query Validation", context);

    const ModeDesc& desc = kModes[mode];
    Constants constants;
    constants.Rays = DescribeCameraRays(camera, rasterViewport);
    constants.TargetSize[0] = target.GetWidth();
    constants.TargetSize[1] = target.GetHeight();
    constants.Mode = mode;
    constants.NumCategories = (uint32_t)desc.categories.size();
    constants.NumMetrics = (uint32_t)desc.metrics.size();
    constants.HeatmapItem = heatmap.item;
    constants.HeatmapMinLog2 = (float)heatmap.minExponent;
    constants.HeatmapMaxLog2 = (float)std::max(heatmap.maxExponent, heatmap.minExponent + 1);

    UpdateOutputUAV(target);

    // ClearUAV는 dynamic descriptor heap을 bind하므로 scene descriptor heap을 bind하는 RaytracingScene::Bind보다 먼저 한다.
    context.TransitionResource(s_Stats, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.ClearUAV(s_Stats);

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // 이미 UAV state이므로 UAV barrier가 들어가 clear가 끝난 뒤에 shader가 더한다.
    context.TransitionResource(s_Stats, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    scene.Bind(context, kScene);
    context.SetDescriptorTable(kOutputs, s_UAVs);
    context.Dispatch2D(target.GetWidth(), target.GetHeight());

    s_LastFrame = { GetFrameCount(), mode, target.GetWidth(), target.GetHeight(),
        rasterViewport.TopLeftX, rasterViewport.TopLeftY, TemporalEffects::EnableTAA };
    s_HasValidatedFrame = true;
}
