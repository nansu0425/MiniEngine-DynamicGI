#include "Model/DynamicGI/DynamicGI.h"
#include "Model/DynamicGI/PathTracedLighting.h"
#include "Model/DynamicGI/ProbeContribution.h"
#include "Model/DynamicGI/ProbeLighting.h"
#include "Model/DynamicGI/ProbeVisualization.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Editor/TextureViewer.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/UI/ImGuiLayer.h"

#include <imgui.h>
#include <algorithm>
#include <optional>
#include <sstream>
#include <vector>

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
        bool pauseProbeUpdates = false;
        ProbeVisualization::Settings probeSpheres;
        bool inspectPixel = false;
    };

    struct InspectedPixel
    {
        int x;
        int y;
        uint32_t captureId;
    };

    struct PendingPixelReply
    {
        uint32_t captureId;
        ControlChannel::Reply reply;
    };

    Settings s_Settings;
    bool s_ProbeStepRequested = false;
    std::optional<uint32_t> s_SelectedProbe;
    std::optional<InspectedPixel> s_InspectedPixel;
    std::vector<PendingPixelReply> s_PendingPixelReplies;
    std::optional<uint32_t> s_HoveredCorner;

    bool IsIndirectOn(void)
    {
        return s_Settings.indirect != Source::kOff;
    }

    std::optional<uint32_t> GetSelectedProbe(void)
    {
        if (s_SelectedProbe && *s_SelectedProbe >= GetProbeCount(ProbeLighting::GetProbes().grid))
            s_SelectedProbe.reset();
        return s_SelectedProbe;
    }

    std::string FormatCoordinate(const DirectX::XMUINT3& coordinate)
    {
        return std::to_string(coordinate.x) + " " + std::to_string(coordinate.y) + " " + std::to_string(coordinate.z);
    }

    std::string FormatSelectedProbe(void)
    {
        const std::optional<uint32_t> probe = GetSelectedProbe();
        if (!probe)
            return "none";
        return FormatCoordinate(GetProbeCoordinate(ProbeLighting::GetProbes().grid, *probe));
    }

    bool IsInspectingPixel(void)
    {
        return s_Settings.indirect == Source::kProbe && s_Settings.inspectPixel && s_InspectedPixel;
    }

    const ProbeContribution::Surface* GetInspectedContribution(void)
    {
        if (!IsInspectingPixel())
            return nullptr;
        const std::optional<ProbeContribution::Readback>& readback = ProbeContribution::GetReadback();
        if (!readback || readback->captureId != s_InspectedPixel->captureId)
            return nullptr;
        return &readback->surface;
    }

    std::optional<std::string> InspectPixel(int x, int y)
    {
        const RECT scene = ImGuiLayer::GetSceneViewport();
        if (x < scene.left || y < scene.top || x >= scene.right || y >= scene.bottom)
        {
            return "the pixel is not in the scene, which covers x from " + std::to_string(scene.left) + " to " + std::to_string(scene.right - 1) +
                " and y from " + std::to_string(scene.top) + " to " + std::to_string(scene.bottom - 1) + " in client pixels";
        }

        const uint32_t captureId = ProbeContribution::CaptureSurface(DirectX::XMUINT2((uint32_t)(x - scene.left), (uint32_t)(y - scene.top)));
        s_InspectedPixel = InspectedPixel{ x, y, captureId };
        return std::nullopt;
    }

    std::string FormatRGB(const DirectX::XMFLOAT3& color)
    {
        char text[64];
        sprintf_s(text, "%.4f %.4f %.4f", color.x, color.y, color.z);
        return text;
    }

    DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& color, float scale)
    {
        return DirectX::XMFLOAT3(color.x * scale, color.y * scale, color.z * scale);
    }

    bool ShouldUpdateProbes(void)
    {
        return !s_Settings.pauseProbeUpdates || s_ProbeStepRequested || ProbeLighting::GetProgress().updates == 0;
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
    constexpr float kMaxDistanceSharpness = 100.0f;
    constexpr uint32_t kMaxDistanceTexelsPerSide = 32;
    constexpr float kMaxRadiusPerSpacing = 0.5f;

    // Source의 순서이고, Engine Tuning의 저장과 settings 명령이 이 이름으로 읽고 쓴다.
    const char* kSourceNames[] = { "Off", "Path Traced", "Probe" };
    const char* kViewNames[] = { "Lit", "Indirect Irradiance", "Indirect Radiance" };
    const char* kVisibilityNames[] = { "None", "Shadow Ray", "Chebyshev" };
    const char* kFacingNormalNames[] = { "Geometry", "Shading" };

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
        probe.distanceSharpness = std::max(1.0f, std::min(probe.distanceSharpness, kMaxDistanceSharpness));
        probe.distanceTexelsPerSide = std::max(1u, std::min(probe.distanceTexelsPerSide, kMaxDistanceTexelsPerSide));
        probe.backFaceDistanceScale = std::max(0.0f, std::min(probe.backFaceDistanceScale, 1.0f));
        probe.weighting.minFacingWeight = std::max(0.0f, std::min(probe.weighting.minFacingWeight, 1.0f));
        probe.weighting.maxBackFaceRatio = std::max(0.0f, std::min(probe.weighting.maxBackFaceRatio, 1.0f));

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

    const ImVec4 kOccludingMeanColor(1.0f, 0.4f, 0.3f, 1.0f);
    const ImVec4 kInactiveProbeColor(1.0f, 0.2f, 0.2f, 1.0f);

    void ShowDistanceCell(bool comparesDistance, float distance)
    {
        if (comparesDistance)
            ImGui::Text("%.3f", distance);
        else
            ImGui::TextDisabled("-");
    }

    void ShowContributionTable(void)
    {
        if (!s_InspectedPixel)
        {
            ImGui::TextDisabled("Inspected Pixel: none");
            return;
        }
        ImGui::Text("Inspected Pixel: %d %d", s_InspectedPixel->x, s_InspectedPixel->y);
        const ProbeContribution::Surface* contribution = GetInspectedContribution();
        if (contribution == nullptr)
        {
            ImGui::TextDisabled("Measuring the pixel.");
            return;
        }
        if (contribution->hasSurface == 0)
        {
            ImGui::TextDisabled("No surface at the pixel.");
            return;
        }
        const bool hasActiveProbe = std::any_of(std::begin(contribution->corners), std::end(contribution->corners),
            [](const ProbeContribution::Corner& probe) { return probe.activeWeight != 0.0f; });
        if (!hasActiveProbe)
            ImGui::TextWrapped("No probe around the point is active, so the point gets no indirect light.");
        else if (contribution->fallsBackToTrilinear != 0)
            ImGui::TextWrapped("The visibility and facing weights leave out every probe, so the weights fall back to the trilinear weights "
                "of the active probes as the shading does.");

        const IrradianceProbeGrid& grid = ProbeLighting::GetProbes().grid;
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        const bool comparesDistance = contribution->comparesDistance != 0;
        if (!ImGui::BeginTable("Probe Contribution", 11, flags))
            return;

        ImGui::TableSetupColumn("Probe");
        ImGui::TableSetupColumn("Weight");
        ImGui::TableSetupColumn("Trilinear");
        ImGui::TableSetupColumn("Back Face");
        ImGui::TableSetupColumn("Visibility");
        ImGui::TableSetupColumn("Facing");
        ImGui::TableSetupColumn("Distance");
        ImGui::TableSetupColumn("Mean");
        ImGui::TableSetupColumn("Irradiance");
        ImGui::TableSetupColumn("Contribution");
        ImGui::TableSetupColumn("Coordinate");
        ImGui::TableHeadersRow();

        float weightSum = 0.0f;
        for (uint32_t corner = 0; corner < ProbeContribution::kNumCorners; ++corner)
        {
            const ProbeContribution::Corner& probe = contribution->corners[corner];
            const uint32_t probeIndex = GetProbeIndex(grid, probe.coordinate);
            weightSum += probe.weight;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const char letter[] = { ProbeContribution::GetCornerLetter(corner), '\0' };
            if (ImGui::Selectable(letter, GetSelectedProbe() == probeIndex, ImGuiSelectableFlags_SpanAllColumns))
                s_SelectedProbe = probeIndex;
            if (ImGui::IsItemHovered())
                s_HoveredCorner = corner;
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", probe.weight * 100.0f);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", probe.trilinearWeight);
            ImGui::TableNextColumn();
            if (probe.activeWeight == 0.0f)
                ImGui::TextColored(kInactiveProbeColor, "%.3f", probe.backFaceRatio);
            else
                ImGui::Text("%.3f", probe.backFaceRatio);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", probe.visibilityWeight);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", probe.facingWeight);
            ImGui::TableNextColumn();
            ShowDistanceCell(comparesDistance, probe.distance);
            ImGui::TableNextColumn();
            if (comparesDistance && probe.distance > probe.distanceMean)
                ImGui::TextColored(kOccludingMeanColor, "%.3f", probe.distanceMean);
            else
                ShowDistanceCell(comparesDistance, probe.distanceMean);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatRGB(probe.irradiance).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatRGB(Scale(probe.irradiance, probe.weight)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatCoordinate(probe.coordinate).c_str());
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Sum");
        ImGui::TableNextColumn();
        ImGui::Text("%.1f%%", weightSum * 100.0f);
        ImGui::TableSetColumnIndex(9);
        ImGui::TextUnformatted(FormatRGB(contribution->irradiance).c_str());
        ImGui::EndTable();

        ImGui::Text("Shading Irradiance: %s", FormatRGB(contribution->shadingIrradiance).c_str());
        ImGui::SetItemTooltip("The irradiance the shading function gives at the same point. It matches the sum of the contributions up to rounding.");
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
        ImGui::SliderFloat("Distance Sharpness", &probe.distanceSharpness, 1.0f, kMaxDistanceSharpness);
        ImGui::SetItemTooltip("Exponent on the cosine between a distance texel and a ray that weights the ray's distance in the texel. "
            "Higher values keep only the rays close to the texel direction. The irradiance keeps the cosine itself.");
        TuningSection::MakeItemResettable("Distance Sharpness");
        ImGui::InputScalar("Distance Texels per Side", ImGuiDataType_U32, &probe.distanceTexelsPerSide, &step);
        ImGui::SetItemTooltip("Texels along each side of a probe's octahedral distance map. The irradiance map keeps 8.");
        TuningSection::MakeItemResettable("Distance Texels per Side");
        ImGui::SliderFloat("Back Face Distance Scale", &probe.backFaceDistanceScale, 0.0f, 1.0f);
        ImGui::SetItemTooltip("A probe ray that first hits the back face of a single-sided material records no radiance "
            "and this fraction of the distance it hit, so the points beyond that face see the probe as occluded.");
        TuningSection::MakeItemResettable("Back Face Distance Scale");
        int visibility = (int)probe.weighting.visibility;
        if (ImGui::Combo("Visibility", &visibility, kVisibilityNames, IM_ARRAYSIZE(kVisibilityNames)))
            probe.weighting.visibility = (ProbeWeighting::Visibility)visibility;
        ImGui::SetItemTooltip("Which of the probes around a point the interpolation keeps. None keeps all of them. "
            "Shadow Ray traces a ray from the point to each probe and keeps the probes it reaches. "
            "Chebyshev weights each probe by how likely the distances it stored toward the point leave the point visible.");
        TuningSection::MakeItemResettable("Visibility");
        ImGui::Checkbox("Facing Weight", &probe.weighting.facing);
        ImGui::SetItemTooltip("Weights each probe by how far it lies in front of the surface, along the normal Facing Normal picks, "
            "so the probes behind a thin wall or cloth give less than the ones the surface faces.");
        TuningSection::MakeItemResettable("Facing Weight");
        ImGui::SliderFloat("Min Facing Weight", &probe.weighting.minFacingWeight, 0.0f, 1.0f);
        ImGui::SetItemTooltip("The facing weight of a probe right behind the surface. A probe the surface faces head-on gets 1 more than this.");
        TuningSection::MakeItemResettable("Min Facing Weight");
        int facingNormal = (int)probe.weighting.facingNormal;
        if (ImGui::Combo("Facing Normal", &facingNormal, kFacingNormalNames, IM_ARRAYSIZE(kFacingNormalNames)))
            probe.weighting.facingNormal = (ProbeWeighting::FacingNormal)facingNormal;
        ImGui::SetItemTooltip("The normal the facing weight measures the side of a probe against. "
            "Geometry is the triangle's normal, constant across each face. Shading is the interpolated vertex normal with the normal map applied.");
        TuningSection::MakeItemResettable("Facing Normal");
        ImGui::Checkbox("Probe Classification", &probe.weighting.classification);
        ImGui::SetItemTooltip("Leaves out of the interpolation the probes whose rays hit the back faces of single-sided materials more often "
            "than Max Back Face Ratio, such as the probes under a floor or inside a wall. "
            "A point with no probe left around it gets no indirect light.");
        TuningSection::MakeItemResettable("Probe Classification");
        ImGui::SliderFloat("Max Back Face Ratio", &probe.weighting.maxBackFaceRatio, 0.0f, 1.0f);
        ImGui::SetItemTooltip("The largest fraction of a probe's rays, averaged over the updates, that may hit back faces "
            "for the probe to stay in the interpolation.");
        TuningSection::MakeItemResettable("Max Back Face Ratio");
        ClampSettings();

        if (ImGui::Button("Reset Probes"))
            ProbeLighting::Reset();
        ImGui::SetItemTooltip("Clears the probes. Changing the grid, the rays per probe, the distance texels per side or the model also clears them.");
        ImGui::Text("Updates: %u", ProbeLighting::GetProgress().updates);
        ImGui::Checkbox("Pause Updates", &s_Settings.pauseProbeUpdates);
        ImGui::SetItemTooltip("Stops tracing and updating the probes. The scene keeps using the probes as they are. "
            "A reset or a grid change waits for the next update.");
        TuningSection::MakeItemResettable("Pause Updates");
        ImGui::SameLine();
        ImGui::BeginDisabled(!s_Settings.pauseProbeUpdates);
        if (ImGui::Button("Step"))
            s_ProbeStepRequested = true;
        ImGui::SetItemTooltip("Updates the probes once on the next frame.");
        ImGui::EndDisabled();

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        ImGui::BeginDisabled(s_Settings.indirect != Source::kProbe);
        ImGui::Checkbox("Show Probes", &probeSpheres.show);
        ImGui::SetItemTooltip("Draws a sphere at each probe. Each point of a sphere shows the probe's irradiance from its normal, "
            "as the radiance of a white diffuse sphere there lit by the indirect light only.");
        TuningSection::MakeItemResettable("Show Probes");
        ImGui::SliderFloat("Probe Radius", &probeSpheres.radiusPerSpacing, 0.0f, kMaxRadiusPerSpacing);
        ImGui::SetItemTooltip("Radius of the spheres as a fraction of the shortest probe spacing. At 0.5 neighbors touch.");
        TuningSection::MakeItemResettable("Probe Radius");
        ImGui::Text("Selected Probe: %s", FormatSelectedProbe().c_str());
        ImGui::SetItemTooltip("Click a probe sphere to select it. Clicking elsewhere in the scene clears the selection.");
        ImGui::Checkbox("Show Texel Means", &probeSpheres.showTexelMeans);
        ImGui::SetItemTooltip("Draws a yellow line from the selected probe along the direction of each of its distance texels, "
            "as long as the mean distance the texel stores.");
        TuningSection::MakeItemResettable("Show Texel Means");
        ImGui::Checkbox("Show Texel Distances", &probeSpheres.showTexelDistances);
        ImGui::SetItemTooltip("Draws a magenta line from the selected probe along the direction of each of its distance texels, "
            "as long as a single ray along that direction hit when the probes were last cleared.");
        TuningSection::MakeItemResettable("Show Texel Distances");
        ImGui::Checkbox("Show Ray Samples", &probeSpheres.showRaySamples);
        ImGui::SetItemTooltip("Draws a line from the selected probe along each ray of its last update, as long as the distance the ray recorded. "
            "Cyan rays hit something closer than the longest distance a probe keeps, and red rays did not.");
        TuningSection::MakeItemResettable("Show Ray Samples");
        ImGui::Checkbox("Inspect Pixel", &s_Settings.inspectPixel);
        ImGui::SetItemTooltip("Click a point of the scene other than a probe sphere to see the probes its irradiance interpolates. "
            "A line from the point to each probe is as thick as the probe's weight, and dashed where the weight is 0. "
            "A letter by each probe names its row in the table below. Hover a row to pick out its line, and click it to select its probe. "
            "The gray lines are the cell of the probes and the green line is the shading normal. The point stays as the camera moves.");
        TuningSection::MakeItemResettable("Inspect Pixel");
        if (s_Settings.inspectPixel)
            ShowContributionTable();
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
        values["Distance Sharpness"] = MeasurementConditions::FormatFloat(probe.distanceSharpness);
        values["Distance Texels per Side"] = std::to_string(probe.distanceTexelsPerSide);
        values["Back Face Distance Scale"] = MeasurementConditions::FormatFloat(probe.backFaceDistanceScale);
        values["Visibility"] = kVisibilityNames[(int)probe.weighting.visibility];
        values["Facing Weight"] = FormatFlag(probe.weighting.facing);
        values["Min Facing Weight"] = MeasurementConditions::FormatFloat(probe.weighting.minFacingWeight);
        values["Facing Normal"] = kFacingNormalNames[(int)probe.weighting.facingNormal];
        values["Probe Classification"] = FormatFlag(probe.weighting.classification);
        values["Max Back Face Ratio"] = MeasurementConditions::FormatFloat(probe.weighting.maxBackFaceRatio);
        values["Pause Updates"] = FormatFlag(s_Settings.pauseProbeUpdates);

        const ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        values["Show Probes"] = FormatFlag(probeSpheres.show);
        values["Probe Radius"] = MeasurementConditions::FormatFloat(probeSpheres.radiusPerSpacing);
        values["Show Texel Means"] = FormatFlag(probeSpheres.showTexelMeans);
        values["Show Texel Distances"] = FormatFlag(probeSpheres.showTexelDistances);
        values["Show Ray Samples"] = FormatFlag(probeSpheres.showRaySamples);
        values["Inspect Pixel"] = FormatFlag(s_Settings.inspectPixel);
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
        ReadFloat(values, "Distance Sharpness", probe.distanceSharpness);
        ReadUInt(values, "Distance Texels per Side", probe.distanceTexelsPerSide);
        ReadFloat(values, "Back Face Distance Scale", probe.backFaceDistanceScale);
        ReadName(values, "Visibility", kVisibilityNames, probe.weighting.visibility);
        ReadFlag(values, "Facing Weight", probe.weighting.facing);
        ReadFloat(values, "Min Facing Weight", probe.weighting.minFacingWeight);
        ReadName(values, "Facing Normal", kFacingNormalNames, probe.weighting.facingNormal);
        ReadFlag(values, "Probe Classification", probe.weighting.classification);
        ReadFloat(values, "Max Back Face Ratio", probe.weighting.maxBackFaceRatio);
        ReadFlag(values, "Pause Updates", s_Settings.pauseProbeUpdates);

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        ReadFlag(values, "Show Probes", probeSpheres.show);
        ReadFloat(values, "Probe Radius", probeSpheres.radiusPerSpacing);
        ReadFlag(values, "Show Texel Means", probeSpheres.showTexelMeans);
        ReadFlag(values, "Show Texel Distances", probeSpheres.showTexelDistances);
        ReadFlag(values, "Show Ray Samples", probeSpheres.showRaySamples);
        ReadFlag(values, "Inspect Pixel", s_Settings.inspectPixel);
        ClampSettings();
    }

    ControlChannel::Fields DescribeVector(const DirectX::XMFLOAT3& vector)
    {
        return ControlChannel::Fields().Float("x", vector.x).Float("y", vector.y).Float("z", vector.z);
    }

    ControlChannel::Fields DescribeColor(const DirectX::XMFLOAT3& color)
    {
        return ControlChannel::Fields().Float("r", color.x).Float("g", color.y).Float("b", color.z);
    }

    ControlChannel::Fields DescribePixel(void)
    {
        ControlChannel::Fields fields;
        fields.Flag("inspect", s_Settings.inspectPixel);
        if (!s_InspectedPixel)
            return fields.Text("selected", "none");

        fields.Object("selected", ControlChannel::Fields().Integer("x", s_InspectedPixel->x).Integer("y", s_InspectedPixel->y));
        const ProbeContribution::Surface* contribution = GetInspectedContribution();
        fields.Flag("measured", contribution != nullptr);
        if (contribution == nullptr)
            return fields;
        fields.Flag("surface", contribution->hasSurface != 0);
        if (contribution->hasSurface == 0)
            return fields;

        ControlChannel::Fields probes;
        for (uint32_t corner = 0; corner < ProbeContribution::kNumCorners; ++corner)
        {
            const ProbeContribution::Corner& probe = contribution->corners[corner];
            ControlChannel::Fields fieldsOfProbe = ControlChannel::Fields()
                .Text("coordinate", FormatCoordinate(probe.coordinate))
                .Float("trilinear", probe.trilinearWeight)
                .Float("back_face_ratio", probe.backFaceRatio)
                .Flag("active", probe.activeWeight != 0.0f)
                .Float("visibility", probe.visibilityWeight)
                .Float("facing", probe.facingWeight);
            if (contribution->comparesDistance != 0)
                fieldsOfProbe.Float("distance", probe.distance).Float("distance_mean", probe.distanceMean);
            fieldsOfProbe
                .Float("weight", probe.weight)
                .Object("irradiance", DescribeColor(probe.irradiance))
                .Object("contribution", DescribeColor(Scale(probe.irradiance, probe.weight)));
            probes.Object(std::string(1, ProbeContribution::GetCornerLetter(corner)), fieldsOfProbe);
        }
        return fields
            .Object("position", DescribeVector(contribution->position))
            .Object("geometry_normal", DescribeVector(contribution->geometryNormal))
            .Object("shading_normal", DescribeVector(contribution->shadingNormal))
            .Flag("falls_back_to_trilinear", contribution->fallsBackToTrilinear != 0)
            .Object("irradiance", DescribeColor(contribution->irradiance))
            .Object("shading_irradiance", DescribeColor(contribution->shadingIrradiance))
            .Object("probes", probes);
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
                .Text("visibility", kVisibilityNames[(int)s_Settings.probe.weighting.visibility])
                .Flag("paused", s_Settings.pauseProbeUpdates)
                .Text("selected", FormatSelectedProbe())
                .Integer("updates", ProbeLighting::GetProgress().updates)
                .Integer("resets", ProbeLighting::GetProgress().resets)
                .Object("pixel", DescribePixel()));
    }

    TuningSection s_Section("Dynamic GI", ShowSettings, SaveSettings, LoadSettings);

    void ReplyWithStateAfterFrame(const ControlChannel::Reply& reply)
    {
        ControlChannel::AfterFrames(1, [reply] { reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("gi"))); });
    }

    void SelectProbe(const std::string& coordinateText, const ControlChannel::Reply& reply)
    {
        if (coordinateText == "none")
        {
            s_SelectedProbe.reset();
            ReplyWithStateAfterFrame(reply);
            return;
        }

        const IrradianceProbeGrid& grid = ProbeLighting::GetProbes().grid;
        std::istringstream stream(coordinateText);
        DirectX::XMUINT3 coordinate;
        std::string rest;
        if (!(stream >> coordinate.x >> coordinate.y >> coordinate.z) || stream >> rest)
        {
            reply.Error("expected <x> <y> <z> or none: " + coordinateText);
            return;
        }
        if (coordinate.x >= grid.counts[0] || coordinate.y >= grid.counts[1] || coordinate.z >= grid.counts[2])
        {
            reply.Error("the probe grid has " + std::to_string(grid.counts[0]) + " " + std::to_string(grid.counts[1]) + " " +
                std::to_string(grid.counts[2]) + " probes along x y z");
            return;
        }

        s_SelectedProbe = GetProbeIndex(grid, coordinate);
        ReplyWithStateAfterFrame(reply);
    }

    void SelectPixel(const std::string& pixelText, const ControlChannel::Reply& reply)
    {
        if (pixelText == "none")
        {
            s_InspectedPixel.reset();
            ReplyWithStateAfterFrame(reply);
            return;
        }
        if (s_Settings.indirect != Source::kProbe)
        {
            reply.Error("Indirect is not Probe");
            return;
        }
        if (!s_Settings.inspectPixel)
        {
            reply.Error("Inspect Pixel is off");
            return;
        }

        std::istringstream stream(pixelText);
        int x = 0, y = 0;
        std::string rest;
        if (!(stream >> x >> y) || stream >> rest)
        {
            reply.Error("expected <x> <y> or none: " + pixelText);
            return;
        }
        if (const std::optional<std::string> error = InspectPixel(x, y))
        {
            reply.Error(*error);
            return;
        }
        s_PendingPixelReplies.push_back({ s_InspectedPixel->captureId, reply });
    }

    void ReplyToMeasuredPixelSelections(void)
    {
        const std::optional<ProbeContribution::Readback>& readback = ProbeContribution::GetReadback();
        std::erase_if(s_PendingPixelReplies, [&readback](const PendingPixelReply& pending)
        {
            if (!IsInspectingPixel())
            {
                pending.reply.Error("the pixel stopped being inspected before it was measured");
                return true;
            }
            if (!readback || readback->captureId < pending.captureId)
                return false;
            pending.reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("gi")));
            return true;
        });
    }

    void HandleGICommand(const std::string& arguments, const ControlChannel::Reply& reply)
    {
        const std::string selectPrefix = "probe select ";
        if (arguments.rfind(selectPrefix, 0) == 0)
        {
            SelectProbe(arguments.substr(selectPrefix.size()), reply);
            return;
        }
        const std::string pixelPrefix = "pixel select ";
        if (arguments.rfind(pixelPrefix, 0) == 0)
        {
            SelectPixel(arguments.substr(pixelPrefix.size()), reply);
            return;
        }
        if (arguments != "probe step")
        {
            reply.Error("unknown arguments: " + arguments);
            return;
        }
        if (s_Settings.indirect != Source::kProbe)
        {
            reply.Error("Indirect is not Probe");
            return;
        }
        if (!s_Settings.pauseProbeUpdates)
        {
            reply.Error("Pause Updates is off");
            return;
        }

        s_ProbeStepRequested = true;
        ReplyWithStateAfterFrame(reply);
    }
}

void DynamicGI::Initialize(void)
{
    PathTracedLighting::Initialize();
    ProbeLighting::Initialize();
    ProbeVisualization::Initialize();
    ProbeContribution::Initialize();

    const ProbeLighting::Probes probes = ProbeLighting::GetProbes();
    TextureViewer::AddTexture("Probes Irradiance", probes.irradiance);
    TextureViewer::AddTexture("Probes Distance", probes.distance);
    TextureViewer::AddTexture("Probes Texel Distance", probes.texelDistance);
    TextureViewer::AddTexture("Probes Back Face Ratio", probes.backFaceRatio);
    TextureViewer::AddTexture("Probe Ray Samples", probes.raySamples);
    TextureViewer::AddTexture("Probe Screen Irradiance", ProbeLighting::GetScreenIrradiance());
    TextureViewer::AddTexture("Path Traced Indirect Radiance", PathTracedLighting::GetIndirectRadiance());

    ControlChannel::RegisterState("gi", DescribeState);
    ControlChannel::Register("gi", "gi probe step | gi probe select <x> <y> <z>|none | gi pixel select <x> <y>|none. "
        "step updates the probes once on the next frame while Pause Updates is on, as the Step button does, "
        "and fails unless Indirect is Probe and Pause Updates is on. "
        "probe select selects the probe at a grid coordinate, or clears the selection, as clicking a probe sphere or elsewhere in the scene does. "
        "The lines of the selected probe are drawn while Show Probes is on. Both reply with state gi after the next frame. "
        "pixel select inspects the surface at a client pixel, as clicking the scene off the probe spheres does while Inspect Pixel is on, "
        "and fails unless Indirect is Probe and Inspect Pixel is on. The pixel must be in the scene, not under the UI. "
        "It replies with state gi once the surface is measured: probe.pixel has the eight probes the irradiance of the surface interpolates, "
        "named A to H as the letters in the scene, each with its grid coordinate, trilinear weight, back face ratio, whether Probe Classification keeps it in the interpolation (always while Probe Classification is off), visibility weight, facing weight (1 while Facing Weight is off), "
        "the distance from the probe to the surface and the distance mean the probe stores toward the surface while Visibility is Chebyshev, "
        "the weight the interpolation gives it after the classification, visibility and facing weights, its irradiance toward the shading normal and that irradiance times the weight. "
        "irradiance is the sum of those and shading_irradiance what the shading reads there. "
        "pixel select none clears the pixel and replies after the next frame", HandleGICommand);
}

void DynamicGI::Shutdown(void)
{
    ProbeContribution::Shutdown();
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
            if (ShouldUpdateProbes())
            {
                ProbeLighting::Update(context, s_Settings.probe, inputs);
                s_ProbeStepRequested = false;
            }
            if (!ProbeWeighting::InterpolatesTrilinearly(s_Settings.probe.weighting))
            {
                ProbeLighting::EstimateScreenIrradiance(context, s_Settings.probe.weighting, inputs, width, height);
                screenIndirectIrradiance = &ProbeLighting::GetScreenIrradiance();
                context.TransitionResource(*screenIndirectIrradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            else
                probesIndirectIrradiance = &ProbeLighting::GetProbes().irradiance;
            context.TransitionResource(ProbeLighting::GetProbes().irradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    }
    Renderer::SetIndirectRadiance(indirectRadiance);
    Renderer::SetProbesIndirectIrradiance(probesIndirectIrradiance, ProbeLighting::GetProbes().grid);
    Renderer::SetScreenIndirectIrradiance(screenIndirectIrradiance);
    Renderer::SetShadingOutput(s_Settings.view);
}

void DynamicGI::UpdateSceneInspection(const Math::Camera& camera)
{
    if (s_Settings.indirect != Source::kProbe)
        return;

    const IrradianceProbeGrid& grid = ProbeLighting::GetProbes().grid;
    if (SceneEditor::IsSceneClicked())
    {
        const bool showsProbes = s_Settings.probeSpheres.show;
        const std::optional<uint32_t> probe =
            showsProbes ? ProbeVisualization::FindProbeUnderCursor(camera, grid, s_Settings.probeSpheres.radiusPerSpacing) : std::nullopt;
        if (s_Settings.inspectPixel && !probe)
        {
            const ImVec2 cursor = ImGui::GetMousePos();
            InspectPixel((int)floorf(cursor.x), (int)floorf(cursor.y));
        }
        else if (showsProbes)
            s_SelectedProbe = probe;
    }

    const ProbeContribution::Surface* contribution = GetInspectedContribution();
    if (contribution != nullptr && contribution->hasSurface != 0)
        ProbeVisualization::AnnotateContribution(camera, grid, *contribution, s_HoveredCorner);
}

void DynamicGI::DrawProbes(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
{
    if (s_Settings.indirect != Source::kProbe || !s_Settings.probeSpheres.show)
        return;

    const ProbeLighting::Probes probes = ProbeLighting::GetProbes();
    ProbeVisualization::DrawSpheres(context, camera, viewport, scissor, probes, s_Settings.probe.weighting, s_Settings.probeSpheres.radiusPerSpacing);

    if (const std::optional<uint32_t> probe = GetSelectedProbe())
        ProbeVisualization::DrawDistanceLines(context, camera, viewport, scissor, probes, *probe, s_Settings.probeSpheres);
}

void DynamicGI::DrawProbeOverlay(GraphicsContext& context, const Math::Camera& camera)
{
    const ProbeContribution::Surface* contribution = GetInspectedContribution();
    if (contribution != nullptr && contribution->hasSurface != 0)
        ProbeVisualization::DrawContribution(context, camera, ProbeLighting::GetProbes().grid, ProbeContribution::GetSurfaceBuffer(), s_HoveredCorner);
    s_HoveredCorner.reset();
}

void DynamicGI::MeasureProbeContribution(const RaytracingScene& scene)
{
    if (IsInspectingPixel() && GetProbeCount(ProbeLighting::GetProbes().grid) != 0)
        ProbeContribution::Measure(s_Settings.probe.weighting, scene);
    ReplyToMeasuredPixelSelections();
}
