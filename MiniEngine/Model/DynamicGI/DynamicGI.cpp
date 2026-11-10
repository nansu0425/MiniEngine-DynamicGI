#include "Model/DynamicGI/DynamicGI.h"
#include "Model/DynamicGI/PathTracedLighting.h"
#include "Model/DynamicGI/ProbeLighting.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/ColorBuffer.h"

#include <imgui.h>
#include <algorithm>

using namespace Graphics;

namespace
{
    enum class Source
    {
        kOff,
        kPathTraced,
        kProbe,
    };

    struct Settings
    {
        Source diffuse = Source::kOff;
        PathTracedLighting::Settings pathTraced;
        ProbeLighting::Settings probe;
    };

    Settings s_Settings;

    bool IsAnyTermOn(void)
    {
        return s_Settings.diffuse != Source::kOff;
    }

    // --- Engine Tuning과 harness --------------------------------------------------------------------

    constexpr uint32_t kMaxSamplesPerFrame = 256;
    // 누적 buffer가 sample 수를 float로 세므로 1씩 더해도 정확한 2^24보다 작게 둔다.
    constexpr uint32_t kMaxTargetSamples = 1u << 20;
    // albedo가 1에 가까운 scene에서도 한 dispatch가 끝나는 시간을 이 상한이 정한다.
    constexpr uint32_t kMaxHits = 1u << 10;
    constexpr uint32_t kMaxProbesPerAxis = 64;
    constexpr uint32_t kMaxRaysPerProbe = 1024;
    constexpr float kMaxHysteresis = 0.999f;

    // Source의 순서이고, Engine Tuning의 저장과 settings 명령이 이 이름으로 읽고 쓴다.
    const char* kSourceNames[] = { "Off", "Path Traced", "Probe" };

    void ClampSettings(void)
    {
        PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        pathTraced.samplesPerFrame = std::max(1u, std::min(pathTraced.samplesPerFrame, kMaxSamplesPerFrame));
        pathTraced.targetSamples = std::max(1u, std::min(pathTraced.targetSamples, kMaxTargetSamples));
        pathTraced.maxHits = std::max(1u, std::min(pathTraced.maxHits, kMaxHits));

        ProbeLighting::Settings& probe = s_Settings.probe;
        for (uint32_t& count : probe.probeCounts)
            count = std::max(2u, std::min(count, kMaxProbesPerAxis));
        probe.raysPerProbe = std::max(1u, std::min(probe.raysPerProbe, kMaxRaysPerProbe));
        probe.hysteresis = std::max(0.0f, std::min(probe.hysteresis, kMaxHysteresis));
    }

    void ShowSettings(void)
    {
        int diffuse = (int)s_Settings.diffuse;
        if (ImGui::Combo("Diffuse", &diffuse, kSourceNames, IM_ARRAYSIZE(kSourceNames)))
            s_Settings.diffuse = (Source)diffuse;
        ImGui::SetItemTooltip("How the raster finds the diffuse light reflected from indirect light at the surface it samples. "
            "Path Traced accumulates it over frames. Probe interpolates the irradiance of a probe grid updated every frame.");
        TuningSection::MakeItemResettable("Diffuse");

        ImGui::SeparatorText("Path Traced");
        PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        const uint32_t step = 1;
        ImGui::InputScalar("Samples per Frame", ImGuiDataType_U32, &pathTraced.samplesPerFrame, &step);
        TuningSection::MakeItemResettable("Samples per Frame");
        ImGui::InputScalar("Target Samples", ImGuiDataType_U32, &pathTraced.targetSamples);
        ImGui::SetItemTooltip("Stops tracing at this many samples per pixel and keeps the average.");
        TuningSection::MakeItemResettable("Target Samples");
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &pathTraced.seed, &step);
        ImGui::SetItemTooltip("Accumulations that differ only in the seed are independent estimates of the same image.");
        TuningSection::MakeItemResettable("Seed");
        ImGui::InputScalar("Max Hits", ImGuiDataType_U32, &pathTraced.maxHits, &step);
        ImGui::SetItemTooltip("Most points a path from the surface hits. 1 sees only the emission and the directional light at the first hit, "
            "the single bounce of DDGI.");
        TuningSection::MakeItemResettable("Max Hits");
        ImGui::Checkbox("Russian Roulette", &pathTraced.russianRoulette);
        ImGui::SetItemTooltip("After the first few hits, ends paths at random by their throughput and weights the survivors up. Unbiased.");
        TuningSection::MakeItemResettable("Russian Roulette");
        ClampSettings();

        if (ImGui::Button("Reset"))
            PathTracedLighting::Reset();
        ImGui::SetItemTooltip("Starts over. Moving the camera, resizing the scene or changing a setting above other than the sample counts also starts over.");

        const PathTracedLighting::Progress progress = PathTracedLighting::GetProgress();
        ImGui::Text("Samples: %u / %u", progress.samples, pathTraced.targetSamples);

        ImGui::SeparatorText("Probe");
        ProbeLighting::Settings& probe = s_Settings.probe;
        const char* axisNames[] = { "Probes X", "Probes Y", "Probes Z" };
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            ImGui::InputScalar(axisNames[axis], ImGuiDataType_U32, &probe.probeCounts[axis], &step);
            ImGui::SetItemTooltip("Probes along this axis of a uniform grid over the model's bounds. Each probe sits at the center of its cell.");
            TuningSection::MakeItemResettable(axisNames[axis]);
        }
        ImGui::InputScalar("Rays per Probe", ImGuiDataType_U32, &probe.raysPerProbe, &step);
        TuningSection::MakeItemResettable("Rays per Probe");
        ImGui::SliderFloat("Hysteresis", &probe.hysteresis, 0.0f, kMaxHysteresis);
        ImGui::SetItemTooltip("How much of the old irradiance each update keeps.");
        TuningSection::MakeItemResettable("Hysteresis");
        ClampSettings();

        if (ImGui::Button("Reset Probes"))
            ProbeLighting::Reset();
        ImGui::SetItemTooltip("Clears the probes. Changing the grid, the rays per probe or the model also clears them.");
        ImGui::Text("Updates: %u", ProbeLighting::GetProgress().updates);
    }

    std::string FormatFlag(bool value)
    {
        return value ? "1" : "0";
    }

    void SaveSettings(TuningSection::Values& values)
    {
        const PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        values["Diffuse"] = kSourceNames[(int)s_Settings.diffuse];
        values["Samples per Frame"] = std::to_string(pathTraced.samplesPerFrame);
        values["Target Samples"] = std::to_string(pathTraced.targetSamples);
        values["Seed"] = std::to_string(pathTraced.seed);
        values["Max Hits"] = std::to_string(pathTraced.maxHits);
        values["Russian Roulette"] = FormatFlag(pathTraced.russianRoulette);

        const ProbeLighting::Settings& probe = s_Settings.probe;
        values["Probes X"] = std::to_string(probe.probeCounts[0]);
        values["Probes Y"] = std::to_string(probe.probeCounts[1]);
        values["Probes Z"] = std::to_string(probe.probeCounts[2]);
        values["Rays per Probe"] = std::to_string(probe.raysPerProbe);
        values["Hysteresis"] = MeasurementConditions::FormatFloat(probe.hysteresis);
    }

    void ReadUInt(const TuningSection::Values& values, const char* key, uint32_t& value)
    {
        const auto text = values.find(key);
        uint32_t read = 0;
        if (text != values.end() && MeasurementConditions::ReadUInt(text->second, read))
            value = read;
    }

    void ReadFloat(const TuningSection::Values& values, const char* key, float& value)
    {
        const auto text = values.find(key);
        float read = 0.0f;
        if (text != values.end() && MeasurementConditions::ReadFloat(text->second, read))
            value = read;
    }

    void ReadFlag(const TuningSection::Values& values, const char* key, bool& value)
    {
        const auto text = values.find(key);
        if (text != values.end() && (text->second == "0" || text->second == "1"))
            value = text->second == "1";
    }

    void ReadSource(const TuningSection::Values& values, const char* key, Source& value)
    {
        const auto text = values.find(key);
        if (text == values.end())
            return;
        const auto name = std::find(std::begin(kSourceNames), std::end(kSourceNames), text->second);
        if (name != std::end(kSourceNames))
            value = (Source)(name - std::begin(kSourceNames));
    }

    void LoadSettings(const TuningSection::Values& values)
    {
        PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        ReadSource(values, "Diffuse", s_Settings.diffuse);
        ReadUInt(values, "Samples per Frame", pathTraced.samplesPerFrame);
        ReadUInt(values, "Target Samples", pathTraced.targetSamples);
        ReadUInt(values, "Seed", pathTraced.seed);
        ReadUInt(values, "Max Hits", pathTraced.maxHits);
        ReadFlag(values, "Russian Roulette", pathTraced.russianRoulette);

        ProbeLighting::Settings& probe = s_Settings.probe;
        ReadUInt(values, "Probes X", probe.probeCounts[0]);
        ReadUInt(values, "Probes Y", probe.probeCounts[1]);
        ReadUInt(values, "Probes Z", probe.probeCounts[2]);
        ReadUInt(values, "Rays per Probe", probe.raysPerProbe);
        ReadFloat(values, "Hysteresis", probe.hysteresis);
        ClampSettings();
    }

    ControlChannel::Fields DescribeState(void)
    {
        const PathTracedLighting::Progress progress = PathTracedLighting::GetProgress();
        return ControlChannel::Fields()
            .Text("diffuse", kSourceNames[(int)s_Settings.diffuse])
            .Object("path_traced", ControlChannel::Fields()
                .Integer("samples", progress.samples)
                .Integer("target", s_Settings.pathTraced.targetSamples)
                .Integer("resets", progress.resets))
            .Object("probe", ControlChannel::Fields()
                .Integer("updates", ProbeLighting::GetProgress().updates)
                .Integer("resets", ProbeLighting::GetProgress().resets));
    }

    TuningSection s_Section("Dynamic GI", ShowSettings, SaveSettings, LoadSettings);
}

void DynamicGI::Initialize(void)
{
    PathTracedLighting::Initialize();
    ProbeLighting::Initialize();

    ControlChannel::RegisterState("gi", DescribeState);
}

void DynamicGI::Shutdown(void)
{
    ProbeLighting::Shutdown();
    PathTracedLighting::Shutdown();
}

void DynamicGI::Reset(void)
{
    PathTracedLighting::Reset();
    ProbeLighting::Reset();
}

void DynamicGI::Update(ComputeContext& context, const GIFrameInputs& inputs, uint32_t width, uint32_t height)
{
    ColorBuffer* diffuseRadiance = nullptr;
    ColorBuffer* probeIrradiance = nullptr;
    if (IsAnyTermOn())
    {
        ScopedTimer _prof(L"Dynamic GI", context);
        if (s_Settings.diffuse == Source::kPathTraced)
        {
            PathTracedLighting::Accumulate(context, s_Settings.pathTraced, inputs, width, height);
            diffuseRadiance = &PathTracedLighting::GetDiffuseRadiance();
            context.TransitionResource(*diffuseRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        else if (s_Settings.diffuse == Source::kProbe)
        {
            ProbeLighting::Update(context, s_Settings.probe, inputs);
            probeIrradiance = &ProbeLighting::GetIrradiance();
            context.TransitionResource(*probeIrradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
    }
    Renderer::SetIndirectDiffuseRadiance(diffuseRadiance);
    Renderer::SetIndirectDiffuseProbes(probeIrradiance, ProbeLighting::GetGrid());
}
