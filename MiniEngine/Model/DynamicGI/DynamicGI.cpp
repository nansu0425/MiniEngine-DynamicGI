#include "Model/DynamicGI/DynamicGI.h"
#include "Model/DynamicGI/PathTracedLighting.h"
#include "Model/DynamicGI/ProbeLighting.h"
#include "Model/DynamicGI/ProbeVisualization.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Editor/TextureViewer.h"
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
        Source indirect = Source::kOff;
        Renderer::ShadingOutput view = Renderer::ShadingOutput::kLit;
        PathTracedLighting::Settings pathTraced;
        ProbeLighting::Settings probe;
        ProbeVisualization::Settings probeSpheres;
    };

    Settings s_Settings;

    bool IsIndirectOn(void)
    {
        return s_Settings.indirect != Source::kOff;
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
    constexpr float kMaxRadiusPerSpacing = 0.5f;

    // Source의 순서이고, Engine Tuning의 저장과 settings 명령이 이 이름으로 읽고 쓴다.
    const char* kSourceNames[] = { "Off", "Path Traced", "Probe" };
    const char* kViewNames[] = { "Lit", "Indirect Irradiance", "Indirect Radiance" };
    const char* kVisibilityNames[] = { "None", "Shadow Ray" };

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

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        probeSpheres.radiusPerSpacing = std::max(0.0f, std::min(probeSpheres.radiusPerSpacing, kMaxRadiusPerSpacing));
    }

    void ShowViewCombo(void)
    {
        if (ImGui::BeginCombo("View", kViewNames[(int)s_Settings.view]))
        {
            for (int view = 0; view < IM_ARRAYSIZE(kViewNames); ++view)
            {
                const bool needsProbes = (Renderer::ShadingOutput)view == Renderer::ShadingOutput::kIndirectIrradiance;
                const ImGuiSelectableFlags flags = needsProbes && s_Settings.indirect != Source::kProbe ? ImGuiSelectableFlags_Disabled : 0;
                const bool selected = (int)s_Settings.view == view;
                if (ImGui::Selectable(kViewNames[view], selected, flags))
                    s_Settings.view = (Renderer::ShadingOutput)view;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("What the raster writes for the opaque surfaces. Lit adds the emission, the directional light and the indirect light. "
            "Indirect Irradiance shows the probe irradiance from the shading normal, the same way Show Probes does, and needs the Probe source. "
            "Indirect Radiance shows the indirect light reflected toward the camera only. Without indirect light both views are black.");
        TuningSection::MakeItemResettable("View");
    }

    void ShowSettings(void)
    {
        int indirect = (int)s_Settings.indirect;
        if (ImGui::Combo("Indirect", &indirect, kSourceNames, IM_ARRAYSIZE(kSourceNames)))
            s_Settings.indirect = (Source)indirect;
        ImGui::SetItemTooltip("How the raster finds the light reflected from indirect light at the surface it samples. "
            "Path Traced accumulates it over frames with the whole glTF BRDF. Probe interpolates the irradiance of a probe grid updated every frame "
            "and reflects it with the diffuse lobe only.");
        TuningSection::MakeItemResettable("Indirect");

        ShowViewCombo();

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
        int visibility = (int)probe.visibility;
        if (ImGui::Combo("Visibility", &visibility, kVisibilityNames, IM_ARRAYSIZE(kVisibilityNames)))
            probe.visibility = (ProbeLighting::Visibility)visibility;
        ImGui::SetItemTooltip("Which of the probes around a point the interpolation keeps. None keeps all of them. "
            "Shadow Ray traces a ray from the point to each probe and keeps the probes it reaches.");
        TuningSection::MakeItemResettable("Visibility");
        ClampSettings();

        if (ImGui::Button("Reset Probes"))
            ProbeLighting::Reset();
        ImGui::SetItemTooltip("Clears the probes. Changing the grid, the rays per probe or the model also clears them.");
        ImGui::Text("Updates: %u", ProbeLighting::GetProgress().updates);

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        ImGui::BeginDisabled(s_Settings.indirect != Source::kProbe);
        ImGui::Checkbox("Show Probes", &probeSpheres.show);
        ImGui::SetItemTooltip("Draws a sphere at each probe. Each point of a sphere shows the probe's irradiance from its normal, "
            "as the radiance of a white diffuse sphere there lit by the indirect light only.");
        TuningSection::MakeItemResettable("Show Probes");
        ImGui::SliderFloat("Probe Radius", &probeSpheres.radiusPerSpacing, 0.0f, kMaxRadiusPerSpacing);
        ImGui::SetItemTooltip("Radius of the spheres as a fraction of the shortest probe spacing. At 0.5 neighbors touch.");
        TuningSection::MakeItemResettable("Probe Radius");
        ImGui::EndDisabled();
        ClampSettings();
    }

    std::string FormatFlag(bool value)
    {
        return value ? "1" : "0";
    }

    void SaveSettings(TuningSection::Values& values)
    {
        const PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        values["Indirect"] = kSourceNames[(int)s_Settings.indirect];
        values["View"] = kViewNames[(int)s_Settings.view];
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
        values["Visibility"] = kVisibilityNames[(int)probe.visibility];

        const ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        values["Show Probes"] = FormatFlag(probeSpheres.show);
        values["Probe Radius"] = MeasurementConditions::FormatFloat(probeSpheres.radiusPerSpacing);
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

    template <typename Enum, size_t NumNames>
    void ReadName(const TuningSection::Values& values, const char* key, const char* const (&names)[NumNames], Enum& value)
    {
        const auto text = values.find(key);
        if (text == values.end())
            return;
        const auto name = std::find(std::begin(names), std::end(names), text->second);
        if (name != std::end(names))
            value = (Enum)(name - std::begin(names));
    }

    void LoadSettings(const TuningSection::Values& values)
    {
        PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        ReadName(values, "Indirect", kSourceNames, s_Settings.indirect);
        ReadName(values, "View", kViewNames, s_Settings.view);
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
        ReadName(values, "Visibility", kVisibilityNames, probe.visibility);

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        ReadFlag(values, "Show Probes", probeSpheres.show);
        ReadFloat(values, "Probe Radius", probeSpheres.radiusPerSpacing);
        ClampSettings();
    }

    ControlChannel::Fields DescribeState(void)
    {
        const PathTracedLighting::Progress progress = PathTracedLighting::GetProgress();
        return ControlChannel::Fields()
            .Text("indirect", kSourceNames[(int)s_Settings.indirect])
            .Text("view", kViewNames[(int)s_Settings.view])
            .Object("path_traced", ControlChannel::Fields()
                .Integer("samples", progress.samples)
                .Integer("target", s_Settings.pathTraced.targetSamples)
                .Integer("resets", progress.resets))
            .Object("probe", ControlChannel::Fields()
                .Text("visibility", kVisibilityNames[(int)s_Settings.probe.visibility])
                .Integer("updates", ProbeLighting::GetProgress().updates)
                .Integer("resets", ProbeLighting::GetProgress().resets));
    }

    TuningSection s_Section("Dynamic GI", ShowSettings, SaveSettings, LoadSettings);
}

void DynamicGI::Initialize(void)
{
    PathTracedLighting::Initialize();
    ProbeLighting::Initialize();
    ProbeVisualization::Initialize();

    TextureViewer::AddTexture("Probes Irradiance", ProbeLighting::GetProbesIrradiance());
    TextureViewer::AddTexture("Probe Ray Radiance", ProbeLighting::GetRayRadiance());
    TextureViewer::AddTexture("Probe Screen Irradiance", ProbeLighting::GetScreenIrradiance());
    TextureViewer::AddTexture("Path Traced Indirect Radiance", PathTracedLighting::GetIndirectRadiance());

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
    ColorBuffer* indirectRadiance = nullptr;
    ColorBuffer* probesIndirectIrradiance = nullptr;
    ColorBuffer* screenIndirectIrradiance = nullptr;
    if (IsIndirectOn())
    {
        ScopedTimer _prof(L"Dynamic GI", context);
        if (s_Settings.indirect == Source::kPathTraced)
        {
            PathTracedLighting::Accumulate(context, s_Settings.pathTraced, inputs, width, height);
            indirectRadiance = &PathTracedLighting::GetIndirectRadiance();
            context.TransitionResource(*indirectRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        else if (s_Settings.indirect == Source::kProbe)
        {
            ProbeLighting::Update(context, s_Settings.probe, inputs);
            if (s_Settings.probe.visibility == ProbeLighting::Visibility::kShadowRay)
            {
                ProbeLighting::EstimateScreenIrradiance(context, inputs, width, height);
                screenIndirectIrradiance = &ProbeLighting::GetScreenIrradiance();
                context.TransitionResource(*screenIndirectIrradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            else
                probesIndirectIrradiance = &ProbeLighting::GetProbesIrradiance();
            context.TransitionResource(ProbeLighting::GetProbesIrradiance(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    }
    Renderer::SetIndirectRadiance(indirectRadiance);
    Renderer::SetProbesIndirectIrradiance(probesIndirectIrradiance, ProbeLighting::GetGrid());
    Renderer::SetScreenIndirectIrradiance(screenIndirectIrradiance);
    Renderer::SetShadingOutput(s_Settings.view);
}

void DynamicGI::DrawProbes(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
{
    if (s_Settings.indirect != Source::kProbe || !s_Settings.probeSpheres.show)
        return;

    ProbeVisualization::DrawSpheres(context, camera, viewport, scissor, ProbeLighting::GetProbesIrradiance(), ProbeLighting::GetGrid(),
        s_Settings.probeSpheres.radiusPerSpacing);
}
