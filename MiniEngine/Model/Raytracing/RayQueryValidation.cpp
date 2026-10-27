#include "Model/Raytracing/RayQueryValidation.h"
#include "Model/Raytracing/CameraRays.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Camera/Camera.h"
#include "Core/Display/Display.h"
#include "Core/Effects/TemporalEffects/TemporalEffects.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/DepthBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"

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
    // Heatmap은 처음에 첫 category를 칠하므로 0이어야 하는 category부터 둔다.
    const ModeDesc kModes[] =
    {
        { "Depth Comparison", { "Depth Mismatch", "Raster Only Hit", "Ray Only Hit", "Alpha Test Mismatch", "Depth Match", "Both Miss" },
            { { "Depth Error", -24, -8 } } },
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
        // camera와 directional light처럼 app이 정한 조건
        RayQueryValidation::SceneConditions scene;
    };

    struct Capture
    {
        ValidatedFrame frame;
        Stats stats;
    };

    // Restore한 Log 한 줄의 조건 중 Restore가 되돌리지 못하는 것. 지금 상태와 견줘 다른 점을 보여 준다.
    struct RestoredLine
    {
        std::string mode;
        uint32_t width;
        uint32_t height;
        bool taa;
        bool sceneRestored;
    };

    enum RootParameter { kConstants, kScene, kRasterDepth = kScene + RaytracingScene::kNumRootParameters, kOutputs, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Ray Query Validation");

    ByteAddressBuffer s_Stats;
    ReadbackBuffer s_StatsReadback;
    SceneHeapTable s_RasterDepthSRV;
    SceneHeapTable s_UAVs;
    // RayQueryValidationCS.hlsl의 Output과 RayQueryValidationStats.hlsli의 Stats가 쓰는 u register의 순서다.
    enum UAV { kOutputUAV, kStatsUAV, kNumUAVs };

    bool s_HasValidatedFrame = false;
    ValidatedFrame s_LastFrame;
    bool s_HasCapture = false;
    Capture s_Capture;

    std::function<void(RayQueryValidation::SceneConditions&)> s_DescribeScene;
    std::function<bool(const RayQueryValidation::SceneConditions&)> s_RestoreScene;
    std::vector<std::string> s_SceneStatePaths;
    std::function<void(const ControlChannel::Reply&)> s_Fit;
    // Restore할 Log 한 줄. Capture하면 그 Capture의 줄로 채운다.
    char s_RestoreLine[4096] = {};
    bool s_HasRestored = false;
    RestoredLine s_Restored;

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
        XMFLOAT3 CameraForward;
        float NearZ;
        float FarZ;
    };

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

    const char* kLogPrefix = "[Ray Query Validation] ";

    // Capture의 조건과 수치를 "key=value; key=value" 한 줄로 쓴다. 숫자는 다시 읽어도 같은 float가 되는 자릿수로 쓴다.
    std::string DescribeCapture(const Capture& capture)
    {
        const ValidatedFrame& frame = capture.frame;
        const ModeDesc& mode = kModes[frame.mode];
        const Stats& stats = capture.stats;

        std::string conditions;
        const auto add = [&conditions](const std::string& key, const std::string& value) { MeasurementConditions::AppendValue(conditions, key, value); };
        char text[256];

        add("mode", mode.label);
        sprintf_s(text, "%ux%u", frame.width, frame.height);
        add("size", text);
        add("taa", frame.taa ? "on" : "off");
        sprintf_s(text, "%.9g,%.9g", frame.jitterX, frame.jitterY);
        add("jitter", text);
        for (const auto& condition : frame.scene)
            add(condition.first, condition.second);

        add("pixels", std::to_string(stats.pixelCount));
        for (size_t category = 0; category < mode.categories.size(); ++category)
            add(mode.categories[category], std::to_string(stats.categoryCounts[category]));
        for (size_t index = 0; index < mode.metrics.size(); ++index)
        {
            const Stats::Metric& metric = stats.metrics[index];
            const uint64_t samples = CountSamples(metric);
            float max;
            std::memcpy(&max, &metric.maxBits, sizeof(max));
            if (samples == 0)
                sprintf_s(text, "samples 0");
            else
                sprintf_s(text, "samples %llu, p50 %s, p99 %s, max %.9g", samples,
                    DescribeBin(FindPercentileBin(metric, samples, 0.5)).c_str(), DescribeBin(FindPercentileBin(metric, samples, 0.99)).c_str(), max);
            add(mode.metrics[index].label, text);
        }
        return kLogPrefix + conditions;
    }

    // Restore가 되돌리는 조건만 Log 줄의 형식으로 쓴다.
    std::string DescribeRestorableConditions(void)
    {
        RayQueryValidation::SceneConditions scene;
        if (s_DescribeScene)
            s_DescribeScene(scene);

        std::string conditions;
        MeasurementConditions::AppendValue(conditions, "taa", TemporalEffects::EnableTAA ? "on" : "off");
        for (const auto& condition : scene)
            MeasurementConditions::AppendValue(conditions, condition.first, condition.second);
        return conditions;
    }

    // TAA는 직접 되돌리고 camera와 directional light는 app에 맡긴다. 해상도와 mode는 사용자가 바꾸도록 차이만 기록한다.
    void Restore(const std::string& line)
    {
        const RayQueryValidation::SceneConditions values = MeasurementConditions::Parse(line);
        const auto value = [&values](const char* key)
        {
            const auto found = values.find(key);
            return found != values.end() ? found->second : std::string();
        };

        s_Restored = {};
        s_Restored.mode = value("mode");
        sscanf_s(value("size").c_str(), "%ux%u", &s_Restored.width, &s_Restored.height);
        s_Restored.taa = value("taa") == "on";
        if (!value("taa").empty())
            TemporalEffects::EnableTAA = s_Restored.taa;
        s_Restored.sceneRestored = s_RestoreScene(values);
        s_HasRestored = true;
    }

    // Restore가 되돌리지 못한 조건 중 지금과 다른 것을 알려 준다.
    void ShowRestoredDifferences(void)
    {
        const ImVec4 kWarningColor(1.0f, 0.75f, 0.2f, 1.0f);
        if (!s_Restored.sceneRestored)
            ImGui::TextColored(kWarningColor, "Warning: the camera and light are not restored. The line lacks them.");
        if (s_Restored.taa)
            ImGui::TextColored(kWarningColor, "Warning: TAA was on in the line, so its jitter cannot be reproduced.");

        if (!WasLastFrameValidated() || s_Restored.mode != kModes[s_LastFrame.mode].label)
        {
            ImGui::TextColored(kWarningColor, "Warning: the line was measured with %s. Pick it in the View section.", s_Restored.mode.c_str());
            return;
        }
        if (s_Restored.width != s_LastFrame.width || s_Restored.height != s_LastFrame.height)
        {
            ImGui::TextColored(kWarningColor, "Warning: the resolution is %u x %u in the line but %u x %u now.",
                s_Restored.width, s_Restored.height, s_LastFrame.width, s_LastFrame.height);
        }
    }

    void ShowRestore(void)
    {
        ImGui::SeparatorText("Restore");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##Line", s_RestoreLine, sizeof(s_RestoreLine)))
            s_HasRestored = false;
        ImGui::SetItemTooltip("A line printed by Log. Capture fills in its own line.");

        const bool canRestore = s_RestoreScene && s_RestoreLine[0] != '\0';
        ImGui::BeginDisabled(!canRestore);
        if (ImGui::Button("Restore"))
            Restore(s_RestoreLine);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(s_RestoreScene ?
            "Sets the camera, light and TAA as in the line. The resolution and the TAA jitter are not restored." :
            "Needs the editor camera.");

        if (s_HasRestored)
            ShowRestoredDifferences();
    }

    // Restore 칸을 그 Capture의 줄로 채운다.
    void CaptureForRestore(void)
    {
        CaptureLastFrame();
        strncpy_s(s_RestoreLine, DescribeCapture(s_Capture).c_str(), _TRUNCATE);
        s_HasRestored = false;
    }

    ControlChannel::Fields DescribeCaptureValues(void)
    {
        ControlChannel::Fields values;
        for (const auto& value : MeasurementConditions::Parse(DescribeCapture(s_Capture)))
            values.Text(value.first, value.second);
        return values;
    }

    // panel이 보이는 Capture의 수치. validation capture가 응답하는 values와 같다.
    ControlChannel::Fields DescribeValidation(void)
    {
        ControlChannel::Fields fields;
        fields.Flag("last_frame_validated", WasLastFrameValidated()).Flag("captured", s_HasCapture);
        if (s_HasCapture)
            fields.Object("values", DescribeCaptureValues());
        return fields;
    }

    // panel의 Capture, Log, Restore와 같은 일을 harness가 명령으로 한다.
    void RegisterCommands(void)
    {
        ControlChannel::RegisterState("validation", DescribeValidation);
        ControlChannel::Register("validation", "validation capture | validation fit: capture is like Capture then Log. "
            "fit is like Fit to Capture: it fits the Heatmap Range of the Ray Tracing View settings to p1 ~ p99 of the heatmap's metric in the last capture of this view. "
            "Both need a Validation view", [](const std::string& arguments, const ControlChannel::Reply& reply)
        {
            if (arguments == "fit" && s_Fit)
            {
                s_Fit(reply);
                return;
            }
            if (arguments != "capture")
            {
                reply.Error("usage: validation capture | validation fit");
                return;
            }
            if (!WasLastFrameValidated())
            {
                reply.Error("the last frame was not validated. Pick a Validation view in the Ray Tracing View settings.");
                return;
            }

            CaptureForRestore();
            const std::string line = DescribeCapture(s_Capture);
            Utility::Print((line + "\n").c_str());
            reply.Ok(ControlChannel::Fields().Text("line", line).Object("values", DescribeCaptureValues())
                .Object("state", ControlChannel::GetState("validation")));
        });

        ControlChannel::Register("describe", "describe: the camera, directional light and TAA as a line that restore takes", [](const std::string&, const ControlChannel::Reply& reply)
        {
            if (!s_DescribeScene)
            {
                reply.Error("describe needs the editor camera");
                return;
            }
            reply.Ok(ControlChannel::Fields().Text("line", DescribeRestorableConditions()));
        });

        ControlChannel::Register("restore", "restore <line>: like Restore with a line printed by Log, validation capture or describe", [](const std::string& arguments, const ControlChannel::Reply& reply)
        {
            if (!s_RestoreScene)
            {
                reply.Error("restore needs the editor camera");
                return;
            }
            if (arguments.empty())
            {
                reply.Error("usage: restore <line>");
                return;
            }

            Restore(arguments);
            if (!s_Restored.sceneRestored)
            {
                reply.Error("the line lacks the camera or light. Only TAA was restored.");
                return;
            }
            // TAA는 Engine Tuning에 있다.
            ControlChannel::Fields state = ControlChannel::GetState("settings.Engine Tuning");
            for (const std::string& path : s_SceneStatePaths)
                state.Append(ControlChannel::GetState(path));
            reply.Ok(ControlChannel::Fields().Text("line", DescribeRestorableConditions()).Object("state", state));
        });
    }
}

void RayQueryValidation::SetFitCommand(const std::function<void(const ControlChannel::Reply&)>& fit)
{
    s_Fit = fit;
}

void RayQueryValidation::SetSceneConditions(const std::function<void(SceneConditions&)>& describe, const std::function<bool(const SceneConditions&)>& restore,
    const std::vector<std::string>& statePaths)
{
    s_DescribeScene = describe;
    s_RestoreScene = restore;
    s_SceneStatePaths = statePaths;
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
    s_RootSignature[kRasterDepth].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    s_RootSignature[kOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, kNumUAVs);
    s_RootSignature.Finalize(L"Ray Query Validation");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pRayQueryValidationCS, sizeof(g_pRayQueryValidationCS));
    s_PSO.Finalize();

    s_Stats.Create(L"Ray Query Validation Stats", sizeof(Stats) / 4, 4);
    s_StatsReadback.Create(L"Ray Query Validation Stats Readback", sizeof(Stats) / 4, 4);

    s_RasterDepthSRV.Create(1);
    s_UAVs.Create(kNumUAVs);
    s_UAVs.PointTo(kStatsUAV, s_Stats, s_Stats.GetUAV());

    RegisterCommands();
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
        CaptureForRestore();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(lastFrameValidated ? "Reads the stats of the last frame." : "Pick a Validation view in the View section.");

    ImGui::SameLine();
    ImGui::BeginDisabled(!s_HasCapture);
    if (ImGui::Button("Log"))
        Utility::Print((DescribeCapture(s_Capture) + "\n").c_str());
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Prints the capture with its camera, light, TAA and resolution as one line to the Output window.");

    if (s_HasCapture)
        ShowCapture(s_Capture);

    ShowRestore();
}

void RayQueryValidation::Render(ComputeContext& context, Mode mode, const Heatmap& heatmap, const Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    DepthBuffer& rasterDepth, const RaytracingScene& scene, ColorBuffer& target)
{
    ScopedTimer _prof(L"Ray Query Validation", context);

    // shader는 target의 pixel마다 같은 위치의 raster depth를 읽는다.
    ASSERT(rasterDepth.GetWidth() == target.GetWidth() && rasterDepth.GetHeight() == target.GetHeight());
    // shader의 depth 식과 miss 판정은 reverse Z와 clear 값 0을 전제로 한다.
    ASSERT(camera.GetClearDepth() == 0.0f && rasterDepth.GetClearDepth() == 0.0f);

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
    XMStoreFloat3(&constants.CameraForward, camera.GetForwardVec());
    constants.NearZ = camera.GetNearClip();
    constants.FarZ = camera.GetFarClip();

    s_RasterDepthSRV.PointTo(0, rasterDepth, rasterDepth.GetDepthSRV());
    s_UAVs.PointTo(kOutputUAV, target, target.GetUAV());

    // ClearUAV는 dynamic descriptor heap을 bind하므로 scene descriptor heap을 bind하는 RaytracingScene::Bind보다 먼저 한다.
    context.TransitionResource(s_Stats, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.ClearUAV(s_Stats);

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(rasterDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // 이미 UAV state이므로 UAV barrier가 들어가 clear가 끝난 뒤에 shader가 더한다.
    context.TransitionResource(s_Stats, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    scene.Bind(context, kScene);
    s_RasterDepthSRV.Bind(context, kRasterDepth);
    s_UAVs.Bind(context, kOutputs);
    context.Dispatch2D(target.GetWidth(), target.GetHeight());

    s_LastFrame = { GetFrameCount(), mode, target.GetWidth(), target.GetHeight(),
        rasterViewport.TopLeftX, rasterViewport.TopLeftY, TemporalEffects::EnableTAA };
    // Capture는 다음 frame에 읽으므로 그 사이에 camera가 움직여도 이 frame의 조건이 남도록 지금 받아 둔다.
    if (s_DescribeScene)
        s_DescribeScene(s_LastFrame.scene);
    s_HasValidatedFrame = true;
}
