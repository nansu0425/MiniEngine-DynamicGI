#include "Model/DynamicGI/DynamicGI.h"
#include "Model/DynamicGI/DynamicGIVolume.h"
#include "Model/DynamicGI/DynamicGIVolumeBox.h"
#include "Model/DynamicGI/PathTracedLighting.h"
#include "Model/DynamicGI/ProbeContribution.h"
#include "Model/DynamicGI/ProbeLighting.h"
#include "Model/DynamicGI/ProbeReadback.h"
#include "Model/DynamicGI/ProbeVisualization.h"
#include "Core/App/UserSettings.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Editor/TextureViewer.h"
#include "Core/Scene/SettingValues.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/UI/ImGuiLayer.h"

#include <imgui.h>
#include <algorithm>
#include <numeric>
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
        bool pauseProbeUpdates = false;
        float spikeFluxShare = 0.3f;
        ProbeVisualization::Settings probeSpheres;
        bool inspectPixel = false;
        float inspectLabelScale = 1.0f;
    };

    struct InspectedPixel
    {
        int x;
        int y;
        uint32_t captureId;
    };

    struct SelectedRay
    {
        uint32_t ray;
        uint32_t probe;
        uint32_t updates;
        uint32_t resets;

        bool operator==(const SelectedRay&) const = default;
    };

    struct PendingPixelReply
    {
        uint32_t captureId;
        ControlChannel::Reply reply;
    };

    struct PendingCensusReply
    {
        uint32_t censusId;
        ControlChannel::Reply reply;
    };

    Settings s_Settings;
    bool s_ProbeStepRequested = false;
    bool s_SteppingUntilSpike = false;
    std::optional<uint32_t> s_SelectedProbe;
    std::optional<InspectedPixel> s_InspectedPixel;
    std::vector<PendingPixelReply> s_PendingPixelReplies;
    std::vector<PendingCensusReply> s_PendingCensusReplies;
    std::optional<uint32_t> s_HoveredCorner;
    std::optional<uint32_t> s_HoveredRay;
    std::optional<SelectedRay> s_SelectedRay;

    bool IsIndirectOn(void)
    {
        return s_Settings.indirect != Source::kOff;
    }

    const ProbeLighting::Volume* FindProbeVolume(void)
    {
        if (s_Settings.indirect != Source::kProbe)
            return nullptr;
        const DynamicGIVolumeComponent* volume = FindDynamicGIVolume();
        return volume != nullptr ? &volume->GetVolume() : nullptr;
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

    std::string DescribeRaySample(DirectX::XMUINT2 texel)
    {
        const IrradianceProbeGrid& grid = ProbeLighting::GetProbes().grid;
        if (texel.y >= GetProbeCount(grid))
            return std::string();
        return "probe " + FormatCoordinate(GetProbeCoordinate(grid, texel.y)) + ", ray " + std::to_string(texel.x);
    }

    std::optional<uint32_t> FindInspectedRayOfSelectedProbe(void)
    {
        const std::optional<uint32_t> probe = GetSelectedProbe();
        const std::optional<DirectX::XMUINT2> texel = TextureViewer::GetInspectedTexel(ProbeLighting::GetProbes().raySamples);
        if (!probe || !texel || texel->y != *probe)
            return std::nullopt;
        return texel->x;
    }

    std::optional<uint32_t> FindSelectedRayOfLastUpdate(void)
    {
        const std::optional<uint32_t> probe = GetSelectedProbe();
        const ProbeLighting::Progress progress = ProbeLighting::GetProgress();
        if (!probe || !s_SelectedRay)
            return std::nullopt;
        const SelectedRay sameRayOfLastUpdate = { s_SelectedRay->ray, *probe, progress.updates, progress.resets };
        return s_SelectedRay == sameRayOfLastUpdate ? std::optional<uint32_t>(s_SelectedRay->ray) : std::nullopt;
    }

    std::optional<uint32_t> FindHighlightedRayOfSelectedProbe(void)
    {
        if (s_HoveredRay)
            return s_HoveredRay;
        if (const std::optional<uint32_t> selectedRay = FindSelectedRayOfLastUpdate())
            return selectedRay;
        return FindInspectedRayOfSelectedProbe();
    }

    float GetLuminance(const DirectX::XMFLOAT3& color)
    {
        return 0.212671f * color.x + 0.715160f * color.y + 0.072169f * color.z;
    }

    float GetFlux(const ProbeReadback::ProbeRay& ray)
    {
        return ray.weight * GetLuminance(ray.radiance);
    }

    float GetFluxSum(const ProbeReadback::ProbeRaysReadback& rays)
    {
        return std::accumulate(rays.rays.begin(), rays.rays.end(), 0.0f,
            [](float sum, const ProbeReadback::ProbeRay& ray) { return sum + GetFlux(ray); });
    }

    float GetFluxShare(const ProbeReadback::ProbeRay& ray, float fluxSum)
    {
        return fluxSum > 0.0f ? GetFlux(ray) / fluxSum : 0.0f;
    }

    float GetLargestFluxShare(const ProbeReadback::ProbeRaysReadback& rays)
    {
        const float fluxSum = GetFluxSum(rays);
        float largestShare = 0.0f;
        for (const ProbeReadback::ProbeRay& ray : rays.rays)
            largestShare = std::max(largestShare, GetFluxShare(ray, fluxSum));
        return largestShare;
    }

    float GetGuidedRaysPerUniformRay(const ProbeReadback::ProbeRay& ray)
    {
        return ray.weight > 0.0f ? 1.0f / ray.weight - 1.0f : 0.0f;
    }

    bool IsInspectingPixel(void)
    {
        return FindProbeVolume() != nullptr && s_Settings.inspectPixel && s_InspectedPixel;
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

    const ProbeContribution::Surface* FindLastMeasuredContribution(void)
    {
        const std::optional<ProbeContribution::Readback>& readback = ProbeContribution::GetReadback();
        return readback ? &readback->surface : nullptr;
    }

    std::optional<std::string> FindWhyProbesAreOff(void)
    {
        if (s_Settings.indirect != Source::kProbe)
            return "Indirect is not Probe";
        if (FindDynamicGIVolume() == nullptr)
            return "the scene has no Dynamic GI Volume";
        return std::nullopt;
    }

    std::optional<std::string> FindWhyProbesCannotBeCounted(void)
    {
        if (std::optional<std::string> error = FindWhyProbesAreOff())
            return error;
        if (ProbeLighting::GetProgress().resets == 0)
            return "the probes have not been updated yet";
        return std::nullopt;
    }

    uint32_t CountProbes(void)
    {
        return ProbeReadback::RequestCensus(FindProbeVolume()->settings.weighting.maxBackFaceRatio);
    }

    std::optional<ProbeReadback::ProbeRelocationReadback> FindSelectedProbeRelocation(void)
    {
        const std::optional<uint32_t> probe = GetSelectedProbe();
        if (!probe)
            return std::nullopt;
        return ProbeReadback::FindProbeRelocation(ProbeLighting::GetProbes().grid, *probe);
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

    // --- Dynamic GI Debug panel과 harness --------------------------------------------------------------------

    constexpr uint32_t kMaxSamplesPerFrame = 256;
    // 누적 buffer가 sample 수를 float로 세므로 1씩 더해도 정확한 2^24보다 작게 둔다.
    constexpr uint32_t kMaxTargetSamples = 1u << 20;
    // albedo가 1에 가까운 scene에서도 한 dispatch가 끝나는 시간을 이 상한이 정한다.
    constexpr uint32_t kMaxHits = 1u << 10;
    constexpr float kMaxRadiusPerSpacing = 0.5f;
    constexpr float kMinInspectLabelScale = 1.0f;
    constexpr float kMaxInspectLabelScale = 4.0f;
    // Source의 순서이고, user.ini의 저장과 settings 명령이 이 이름으로 읽고 쓴다.
    const char* kSourceNames[] = { "Off", "Path Traced", "Probe" };
    const char* kViewNames[] = { "Lit", "Indirect Irradiance", "Indirect Radiance" };
    const char* kRaySampleColorNames[] = { "Distance", "Kind", "Contribution" };
    constexpr size_t kNumListedRays = 16;

    void ClampSettings(void)
    {
        PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        pathTraced.samplesPerFrame = std::max(1u, std::min(pathTraced.samplesPerFrame, kMaxSamplesPerFrame));
        pathTraced.targetSamples = std::max(1u, std::min(pathTraced.targetSamples, kMaxTargetSamples));
        pathTraced.maxHits = std::max(1u, std::min(pathTraced.maxHits, kMaxHits));

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        probeSpheres.radiusPerSpacing = std::max(0.0f, std::min(probeSpheres.radiusPerSpacing, kMaxRadiusPerSpacing));

        s_Settings.inspectLabelScale = std::max(kMinInspectLabelScale, std::min(s_Settings.inspectLabelScale, kMaxInspectLabelScale));
        s_Settings.spikeFluxShare = std::max(0.0f, std::min(s_Settings.spikeFluxShare, 1.0f));
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
    }

    const char* kRelocationResultKeys[] = { "not_moved", "back_face_push", "front_face_push", "blocked" };

    void ShowCensus(void)
    {
        const std::optional<ProbeReadback::CensusReadback>& readback = ProbeReadback::GetCensus();
        if (!readback)
            return;
        const ProbeReadback::Census& census = readback->census;
        ImGui::Text("Measured at update %u of reset %u, %u probes:", readback->updates, readback->resets, readback->probes);
        ImGui::Text("Inside Geometry: %u (Max Back Face Ratio %.3f)", census.insideGeometry, readback->maxBackFaceRatio);
        for (uint32_t result = 0; result < (uint32_t)ProbeRelocation::Result::kCount; ++result)
            ImGui::Text("%s: %u", ProbeRelocation::GetResultName((ProbeRelocation::Result)result), census.relocationResults[result]);
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
            ImGui::SameLine();
            ImGui::TextDisabled("(measuring, the table shows the last measured pixel)");
            contribution = FindLastMeasuredContribution();
            if (contribution == nullptr)
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

    void ShowRayTable(void)
    {
        const std::optional<uint32_t> probe = GetSelectedProbe();
        if (!probe)
            return;
        const ProbeReadback::ProbeRaysReadback* rays = ProbeReadback::FindLastReadRays(*probe);
        if (rays == nullptr)
        {
            ImGui::TextDisabled("Rays: reading");
            return;
        }

        const float fluxSum = GetFluxSum(*rays);
        ImGui::Text("Rays of update %u: largest flux share %.1f%%", rays->updates, GetLargestFluxShare(*rays) * 100.0f);
        if (ProbeReadback::FindRaysOfLastUpdate(*probe) == nullptr)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(reading the last update)");
        }

        std::vector<uint32_t> raysByFlux(rays->rays.size());
        std::iota(raysByFlux.begin(), raysByFlux.end(), 0u);
        const size_t listedRays = std::min(raysByFlux.size(), kNumListedRays);
        std::partial_sort(raysByFlux.begin(), raysByFlux.begin() + listedRays, raysByFlux.end(),
            [rays](uint32_t a, uint32_t b) { return GetFlux(rays->rays[a]) > GetFlux(rays->rays[b]); });

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("Probe Rays", 7, flags))
            return;

        ImGui::TableSetupColumn("Ray");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Flux Share");
        ImGui::TableSetupColumn("Weight");
        ImGui::TableSetupColumn("Guided per Uniform");
        ImGui::TableSetupColumn("Luminance");
        ImGui::TableSetupColumn("Hit Distance");
        ImGui::TableHeadersRow();

        for (size_t row = 0; row < listedRays; ++row)
        {
            const uint32_t rayIndex = raysByFlux[row];
            const ProbeReadback::ProbeRay& ray = rays->rays[rayIndex];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const SelectedRay rayOfRow = { rayIndex, rays->probe, rays->updates, rays->resets };
            if (ImGui::Selectable(std::to_string(rayIndex).c_str(), s_SelectedRay == rayOfRow, ImGuiSelectableFlags_SpanAllColumns))
                s_SelectedRay = s_SelectedRay == rayOfRow ? std::nullopt : std::optional<SelectedRay>(rayOfRow);
            if (ImGui::IsItemHovered())
                s_HoveredRay = rayIndex;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(rayIndex < rays->uniformRaysPerProbe ? "Uniform" : "Guided");
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", GetFluxShare(ray, fluxSum) * 100.0f);
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", ray.weight);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", GetGuidedRaysPerUniformRay(ray));
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", GetLuminance(ray.radiance));
            ImGui::TableNextColumn();
            if (ray.hitDistance)
                ImGui::Text("%.3f", *ray.hitDistance);
            else
                ImGui::TextDisabled("miss");
        }
        ImGui::EndTable();
    }

    void ShowDebugPanelContent(void)
    {
        int indirect = (int)s_Settings.indirect;
        if (ImGui::Combo("Indirect", &indirect, kSourceNames, IM_ARRAYSIZE(kSourceNames)))
            s_Settings.indirect = (Source)indirect;
        ImGui::SetItemTooltip("How the raster finds the light reflected from indirect light at the surface it samples. "
            "Path Traced accumulates it over frames with the whole glTF BRDF. Probe interpolates the irradiance of a probe grid updated every frame "
            "and reflects it with the diffuse lobe only.");

        ShowViewCombo();

        ImGui::SeparatorText("Path Traced");
        PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        const uint32_t step = 1;
        ImGui::InputScalar("Samples per Frame", ImGuiDataType_U32, &pathTraced.samplesPerFrame, &step);
        ImGui::InputScalar("Target Samples", ImGuiDataType_U32, &pathTraced.targetSamples);
        ImGui::SetItemTooltip("Stops tracing at this many samples per pixel and keeps the average.");
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &pathTraced.seed, &step);
        ImGui::SetItemTooltip("Accumulations that differ only in the seed are independent estimates of the same image.");
        ImGui::InputScalar("Max Hits", ImGuiDataType_U32, &pathTraced.maxHits, &step);
        ImGui::SetItemTooltip("Most points a path from the surface hits. 1 sees only the emission and the directional light at the first hit, "
            "the single bounce of DDGI.");
        ImGui::Checkbox("Russian Roulette", &pathTraced.russianRoulette);
        ImGui::SetItemTooltip("After the first few hits, ends paths at random by their throughput and weights the survivors up. Unbiased.");
        ClampSettings();

        if (ImGui::Button("Reset"))
            PathTracedLighting::Reset();
        ImGui::SetItemTooltip("Starts over. Moving the camera, resizing the scene or changing a setting above other than the sample counts also starts over.");

        const PathTracedLighting::Progress progress = PathTracedLighting::GetProgress();
        ImGui::Text("Samples: %u / %u", progress.samples, pathTraced.targetSamples);

        ImGui::SeparatorText("Probe");
        if (FindDynamicGIVolume() == nullptr)
        {
            ImGui::BeginDisabled();
            ImGui::TextWrapped("No Dynamic GI Volume in the scene, so the Probe source gives no indirect light.");
            ImGui::EndDisabled();
        }
        if (ImGui::Button("Reset Probes"))
            ProbeLighting::Reset();
        ImGui::SetItemTooltip("Clears the probes. Moving or resizing the Dynamic GI Volume, changing its probe counts, rays per probe, "
            "distance texels per side or Probe Relocation settings, or changing the model also clears them.");
        ImGui::Text("Updates: %u", ProbeLighting::GetProgress().updates);
        ImGui::Checkbox("Pause Updates", &s_Settings.pauseProbeUpdates);
        ImGui::SetItemTooltip("Stops tracing and updating the probes. The scene keeps using the probes as they are. "
            "A reset or a grid change waits for the next update.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!s_Settings.pauseProbeUpdates);
        if (ImGui::Button("Step"))
            s_ProbeStepRequested = true;
        ImGui::SetItemTooltip("Updates the probes once on the next frame.");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!s_Settings.pauseProbeUpdates || !GetSelectedProbe());
        if (ImGui::Button(s_SteppingUntilSpike ? "Stop###Step Until Spike" : "Step Until Spike###Step Until Spike"))
        {
            s_SteppingUntilSpike = !s_SteppingUntilSpike;
            s_ProbeStepRequested = s_SteppingUntilSpike;
        }
        ImGui::SetItemTooltip("Updates the probes one update at a time until a single ray of the selected probe has at least Spike Flux Share "
            "of the flux of the probe's rays, and stops there with the rays of that update. Needs Pause Updates and a selected probe.");
        ImGui::EndDisabled();
        ImGui::SliderFloat("Spike Flux Share", &s_Settings.spikeFluxShare, 0.0f, 1.0f);
        ImGui::SetItemTooltip("The share of the flux of a probe's rays a single ray must have for Step Until Spike to stop. "
            "The flux of a ray is its weight times the luminance of its radiance, as the guide map counts it.");
        ImGui::BeginDisabled(FindWhyProbesCannotBeCounted().has_value());
        if (ImGui::Button("Measure"))
            CountProbes();
        ImGui::SetItemTooltip("Counts the probes Max Back Face Ratio finds inside geometry, from the back face ratios averaged over the updates so far, "
            "and the probes by what the last Probe Relocation iteration did to them.");
        ImGui::EndDisabled();
        ShowCensus();

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        ImGui::BeginDisabled(FindProbeVolume() == nullptr);
        ImGui::Checkbox("Show Probes", &probeSpheres.show);
        ImGui::SetItemTooltip("Draws a sphere at each probe. Each point of a sphere shows the probe's irradiance from its normal, "
            "as the radiance of a white diffuse sphere there lit by the indirect light only.");
        ImGui::SliderFloat("Probe Radius", &probeSpheres.radiusPerSpacing, 0.0f, kMaxRadiusPerSpacing);
        ImGui::SetItemTooltip("Radius of the spheres as a fraction of the shortest probe spacing. At 0.5 neighbors touch.");
        ImGui::Text("Selected Probe: %s", FormatSelectedProbe().c_str());
        ImGui::SetItemTooltip("Click a probe sphere to select it. Clicking elsewhere in the scene clears the selection.");
        if (const std::optional<ProbeReadback::ProbeRelocationReadback> relocation = FindSelectedProbeRelocation())
        {
            ImGui::Text("Selected Probe Offset: %.3f %.3f %.3f (%s)", relocation->offset.x, relocation->offset.y, relocation->offset.z,
                ProbeRelocation::GetResultName(relocation->result));
            ImGui::SetItemTooltip("How far Probe Relocation moved the selected probe from its grid position, and what the last iteration did to it.");
        }
        ImGui::Checkbox("Show Probe Offsets", &probeSpheres.showOffsets);
        ImGui::SetItemTooltip("Draws a line from the grid position of each probe to where Probe Relocation moved it. "
            "Green lines end at a probe the last iteration pushed through a back face, blue ones at a probe it moved away from a front face, "
            "red ones at a probe it could not move within Max Probe Offset, and gray ones at a probe it left where it was.");
        ImGui::Checkbox("Show Texel Means", &probeSpheres.showTexelMeans);
        ImGui::SetItemTooltip("Draws a yellow line from the selected probe along the direction of each of its distance texels, "
            "as long as the mean distance the texel stores.");
        ImGui::Checkbox("Show Texel Distances", &probeSpheres.showTexelDistances);
        ImGui::SetItemTooltip("Draws a magenta line from the selected probe along the direction of each of its distance texels, "
            "as long as a single ray along that direction hit when the probes were last cleared.");
        ImGui::Checkbox("Show Ray Samples", &probeSpheres.showRaySamples);
        ImGui::SetItemTooltip("Draws a line from the selected probe along each ray of its last update, up to the point the ray hit. "
            "A ray that hit nothing ends at the longest distance a probe keeps. Ray Sample Color picks the colors. "
            "While the Texture Viewer shows Probe Ray Samples with a texel of the selected probe's row inspected, or a row of the ray table is hovered or selected, "
            "the ray of that texel or row is white, the rays in the same guide map texel keep their color, and the other lines are faint. "
            "Click a row to select its ray and click it again to clear the selection. The selection lasts until the probe updates. "
            "The table lists the rays of the selected probe with the largest flux, the weight times the luminance of the radiance. "
            "Guided per Uniform is how many guided rays the guide map sent around the ray for each uniform ray.");
        int raySampleColor = (int)probeSpheres.raySampleColor;
        if (ImGui::Combo("Ray Sample Color", &raySampleColor, kRaySampleColorNames, IM_ARRAYSIZE(kRaySampleColorNames)))
            probeSpheres.raySampleColor = (ProbeVisualization::RaySampleColor)raySampleColor;
        ImGui::SetItemTooltip("Distance draws rays that hit something closer than the longest distance a probe keeps in cyan and the others in red. "
            "Kind draws uniform rays in cyan and guided rays in orange. "
            "Contribution draws a ray brighter and more opaque the larger its flux is next to the largest flux of the probe's rays, "
            "from faint blue to yellow.");
        if (probeSpheres.showRaySamples)
            ShowRayTable();
        ImGui::Checkbox("Inspect Pixel", &s_Settings.inspectPixel);
        ImGui::SetItemTooltip("Click a point of the scene other than a probe sphere to see the probes its irradiance interpolates. "
            "A line from the point to each probe is as thick as the probe's weight, and dashed where the weight is 0. "
            "A letter by each probe names its row in the table below. Hover a row to pick out its line, and click it to select its probe. "
            "The gray lines are the cell of the probes and the light blue arrow is the shading normal. "
            "While Visibility is Chebyshev, the lines to the probes start from the point Self Shadow Bias moves off the surface, "
            "which a green arrow from the surface points to. The point stays as the camera moves.");
        if (s_Settings.inspectPixel)
        {
            ImGui::SliderFloat("Inspect Label Scale", &s_Settings.inspectLabelScale, kMinInspectLabelScale, kMaxInspectLabelScale);
            ImGui::SetItemTooltip("Scales the letter and weight boxes drawn by each probe of the inspected pixel.");
            ShowContributionTable();
        }
        ImGui::EndDisabled();
        ClampSettings();
    }

    void SaveSettings(UserSettings::Values& values)
    {
        const PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        values["Indirect"] = kSourceNames[(int)s_Settings.indirect];
        values["View"] = kViewNames[(int)s_Settings.view];
        values["Samples per Frame"] = std::to_string(pathTraced.samplesPerFrame);
        values["Target Samples"] = std::to_string(pathTraced.targetSamples);
        values["Seed"] = std::to_string(pathTraced.seed);
        values["Max Hits"] = std::to_string(pathTraced.maxHits);
        values["Russian Roulette"] = SettingValues::FormatFlag(pathTraced.russianRoulette);

        values["Pause Updates"] = SettingValues::FormatFlag(s_Settings.pauseProbeUpdates);
        values["Spike Flux Share"] = MeasurementConditions::FormatFloat(s_Settings.spikeFluxShare);

        const ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        values["Show Probes"] = SettingValues::FormatFlag(probeSpheres.show);
        values["Probe Radius"] = MeasurementConditions::FormatFloat(probeSpheres.radiusPerSpacing);
        values["Show Texel Means"] = SettingValues::FormatFlag(probeSpheres.showTexelMeans);
        values["Show Texel Distances"] = SettingValues::FormatFlag(probeSpheres.showTexelDistances);
        values["Show Ray Samples"] = SettingValues::FormatFlag(probeSpheres.showRaySamples);
        values["Ray Sample Color"] = kRaySampleColorNames[(int)probeSpheres.raySampleColor];
        values["Show Probe Offsets"] = SettingValues::FormatFlag(probeSpheres.showOffsets);
        values["Inspect Pixel"] = SettingValues::FormatFlag(s_Settings.inspectPixel);
        values["Inspect Label Scale"] = MeasurementConditions::FormatFloat(s_Settings.inspectLabelScale);
    }

    void LoadSettings(const UserSettings::Values& values)
    {
        PathTracedLighting::Settings& pathTraced = s_Settings.pathTraced;
        SettingValues::ReadName(values, "Indirect", kSourceNames, s_Settings.indirect);
        SettingValues::ReadName(values, "View", kViewNames, s_Settings.view);
        SettingValues::ReadUInt(values, "Samples per Frame", pathTraced.samplesPerFrame);
        SettingValues::ReadUInt(values, "Target Samples", pathTraced.targetSamples);
        SettingValues::ReadUInt(values, "Seed", pathTraced.seed);
        SettingValues::ReadUInt(values, "Max Hits", pathTraced.maxHits);
        SettingValues::ReadFlag(values, "Russian Roulette", pathTraced.russianRoulette);

        SettingValues::ReadFlag(values, "Pause Updates", s_Settings.pauseProbeUpdates);
        SettingValues::ReadFloat(values, "Spike Flux Share", s_Settings.spikeFluxShare);

        ProbeVisualization::Settings& probeSpheres = s_Settings.probeSpheres;
        SettingValues::ReadFlag(values, "Show Probes", probeSpheres.show);
        SettingValues::ReadFloat(values, "Probe Radius", probeSpheres.radiusPerSpacing);
        SettingValues::ReadFlag(values, "Show Texel Means", probeSpheres.showTexelMeans);
        SettingValues::ReadFlag(values, "Show Texel Distances", probeSpheres.showTexelDistances);
        SettingValues::ReadFlag(values, "Show Ray Samples", probeSpheres.showRaySamples);
        SettingValues::ReadName(values, "Ray Sample Color", kRaySampleColorNames, probeSpheres.raySampleColor);
        SettingValues::ReadFlag(values, "Show Probe Offsets", probeSpheres.showOffsets);
        SettingValues::ReadFlag(values, "Inspect Pixel", s_Settings.inspectPixel);
        SettingValues::ReadFloat(values, "Inspect Label Scale", s_Settings.inspectLabelScale);
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
            .Object("distance_lookup_position", DescribeVector(contribution->distanceLookupPosition))
            .Object("geometry_normal", DescribeVector(contribution->geometryNormal))
            .Object("shading_normal", DescribeVector(contribution->shadingNormal))
            .Flag("falls_back_to_trilinear", contribution->fallsBackToTrilinear != 0)
            .Object("irradiance", DescribeColor(contribution->irradiance))
            .Object("shading_irradiance", DescribeColor(contribution->shadingIrradiance))
            .Object("probes", probes);
    }

    ControlChannel::Fields DescribeSelectedProbeRelocation(void)
    {
        const std::optional<ProbeReadback::ProbeRelocationReadback> relocation = FindSelectedProbeRelocation();
        if (!relocation)
            return ControlChannel::Fields().Flag("read", false);
        return ControlChannel::Fields()
            .Flag("read", true)
            .Object("offset", DescribeVector(relocation->offset))
            .Text("result", ProbeRelocation::GetResultName(relocation->result));
    }

    ControlChannel::Fields DescribeCensus(void)
    {
        const std::optional<ProbeReadback::CensusReadback>& readback = ProbeReadback::GetCensus();
        if (!readback)
            return ControlChannel::Fields().Flag("measured", false);
        ControlChannel::Fields relocation;
        for (uint32_t result = 0; result < (uint32_t)ProbeRelocation::Result::kCount; ++result)
            relocation.Integer(kRelocationResultKeys[result], readback->census.relocationResults[result]);
        return ControlChannel::Fields()
            .Flag("measured", true)
            .Integer("id", readback->id)
            .Integer("updates", readback->updates)
            .Integer("resets", readback->resets)
            .Integer("probes", readback->probes)
            .Float("max_back_face_ratio", readback->maxBackFaceRatio)
            .Integer("inside_geometry", readback->census.insideGeometry)
            .Object("relocation", relocation);
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
                .Flag("volume", FindDynamicGIVolume() != nullptr)
                .Flag("paused", s_Settings.pauseProbeUpdates)
                .Text("selected", FormatSelectedProbe())
                .Object("selected_relocation", DescribeSelectedProbeRelocation())
                .Integer("updates", ProbeLighting::GetProgress().updates)
                .Integer("resets", ProbeLighting::GetProgress().resets)
                .Object("pixel", DescribePixel())
                .Object("measure", DescribeCensus()));
    }

    const char kDebugPanelName[] = "Dynamic GI Debug";
    bool s_ShowDebugPanel = false;

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
        if (std::optional<std::string> error = FindWhyProbesAreOff())
        {
            reply.Error(*error);
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

    void MeasureProbesForReply(const ControlChannel::Reply& reply)
    {
        if (std::optional<std::string> error = FindWhyProbesCannotBeCounted())
        {
            reply.Error(*error);
            return;
        }
        s_PendingCensusReplies.push_back({ CountProbes(), reply });
    }

    void ReplyToCountedProbes(void)
    {
        const std::optional<ProbeReadback::CensusReadback>& readback = ProbeReadback::GetCensus();
        std::erase_if(s_PendingCensusReplies, [&readback](const PendingCensusReply& pending)
        {
            if (!readback || readback->id < pending.censusId)
                return false;
            pending.reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("gi")));
            return true;
        });
    }

    std::optional<uint32_t> FindProbeOfWatchedRays(void)
    {
        return FindProbeVolume() != nullptr ? GetSelectedProbe() : std::nullopt;
    }

    void StepUntilSpike(void)
    {
        if (!s_SteppingUntilSpike)
            return;
        const std::optional<uint32_t> probe = FindProbeOfWatchedRays();
        if (!probe || !s_Settings.pauseProbeUpdates)
        {
            s_SteppingUntilSpike = false;
            return;
        }
        if (s_ProbeStepRequested)
            return;
        const ProbeReadback::ProbeRaysReadback* rays = ProbeReadback::FindRaysOfLastUpdate(*probe);
        if (rays == nullptr)
            return;

        if (GetLargestFluxShare(*rays) >= s_Settings.spikeFluxShare)
            s_SteppingUntilSpike = false;
        else
            s_ProbeStepRequested = true;
    }

    void HandleGICommand(const std::string& arguments, const ControlChannel::Reply& reply)
    {
        if (arguments == "probe measure")
        {
            MeasureProbesForReply(reply);
            return;
        }
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
        if (std::optional<std::string> error = FindWhyProbesAreOff())
        {
            reply.Error(*error);
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
    ProbeReadback::Initialize();
    DynamicGIVolumeBox::Initialize();

    const ProbeLighting::Probes probes = ProbeLighting::GetProbes();
    TextureViewer::AddTexture("Probes Irradiance", probes.irradiance);
    TextureViewer::AddTexture("Probes Distance", probes.distance);
    TextureViewer::AddTexture("Probes Texel Distance", probes.texelDistance);
    TextureViewer::AddTexture("Probes Back Face Ratio", probes.backFaceRatio);
    TextureViewer::AddTexture("Probes Offset", probes.offset);
    TextureViewer::AddTexture("Probes Guide", probes.guide, {
        .markedRowName = "Selected Probe",
        .findMarkedRow = GetSelectedProbe,
    });
    TextureViewer::AddTexture("Probe Weighted Ray Directions", probes.weightedRayDirections, {
        .markedRowName = "Selected Probe",
        .findMarkedRow = GetSelectedProbe,
        .describeTexel = DescribeRaySample,
    });
    TextureViewer::AddTexture("Probe Ray Samples", probes.raySamples, {
        .markedRowName = "Selected Probe",
        .findMarkedRow = GetSelectedProbe,
        .describeTexel = DescribeRaySample,
    });
    TextureViewer::AddTexture("Probe Emissive Samples", probes.emissiveSamples, {
        .markedRowName = "Selected Probe",
        .findMarkedRow = GetSelectedProbe,
    });
    TextureViewer::AddTexture("Probe Screen Irradiance", ProbeLighting::GetScreenIrradiance());
    TextureViewer::AddTexture("Path Traced Indirect Radiance", PathTracedLighting::GetIndirectRadiance());

    SceneEditor::RegisterComponentDetails<DynamicGIVolumeComponent>(ShowDynamicGIVolumeDetails);
    UserSettings::Register(kDebugPanelName, SaveSettings, LoadSettings);

    ControlChannel::RegisterState("gi", DescribeState);
    ControlChannel::Register("gi", "gi probe step | gi probe select <x> <y> <z>|none | gi probe measure | gi pixel select <x> <y>|none. "
        "step updates the probes once on the next frame while Pause Updates is on, as the Step button does, "
        "and fails unless Indirect is Probe, the scene has a Dynamic GI Volume and Pause Updates is on. "
        "probe select selects the probe at a grid coordinate, or clears the selection, as clicking a probe sphere or elsewhere in the scene does. "
        "The lines of the selected probe are drawn while Show Probes is on. Both reply with state gi after the next frame. "
        "probe.selected_relocation has the offset Probe Relocation moved the selected probe by and what its last iteration did, once read back after a reset. "
        "probe measure counts the probes as the Measure button does and fails unless Indirect is Probe, the scene has a Dynamic GI Volume and the probes were updated. "
        "It replies with state gi once counted: probe.measure has the updates and resets at the count, the probe count, the Max Back Face Ratio it used, "
        "the probes whose back face ratio averaged over the updates is above it, and the probes by what the last Probe Relocation iteration did. "
        "pixel select inspects the surface at a client pixel, as clicking the scene off the probe spheres does while Inspect Pixel is on, "
        "and fails unless Indirect is Probe, the scene has a Dynamic GI Volume and Inspect Pixel is on. The pixel must be in the scene, not under the UI. "
        "It replies with state gi once the surface is measured: probe.pixel has the eight probes the irradiance of the surface interpolates, "
        "named A to H as the letters in the scene, each with its grid coordinate, trilinear weight, back face ratio, whether Probe Classification keeps it in the interpolation (always while Probe Classification is off), visibility weight, facing weight (1 while Facing Weight is off), "
        "the distance from the probe to distance_lookup_position and the distance mean the probe stores toward it while Visibility is Chebyshev, "
        "the weight the interpolation gives it after the classification, visibility and facing weights, its irradiance toward the shading normal and that irradiance times the weight. "
        "distance_lookup_position is the point Self Shadow Bias moves off the surface while Visibility is Chebyshev, and the surface point otherwise. "
        "irradiance is the sum of those and shading_irradiance what the shading reads there. "
        "pixel select none clears the pixel and replies after the next frame", HandleGICommand);
}

void DynamicGI::Shutdown(void)
{
    UserSettings::Unregister(kDebugPanelName);
    ProbeReadback::Shutdown();
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
    if (FindDynamicGIVolume() == nullptr)
        ProbeLighting::Reset();
    if (IsIndirectOn())
    {
        ScopedTimer _prof(L"Dynamic GI", context);
        if (s_Settings.indirect == Source::kPathTraced)
        {
            PathTracedLighting::Accumulate(context, s_Settings.pathTraced, inputs, width, height);
            indirectRadiance = &PathTracedLighting::GetIndirectRadiance();
            context.TransitionResource(*indirectRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        else if (const ProbeLighting::Volume* volume = FindProbeVolume())
        {
            if (ShouldUpdateProbes())
            {
                ProbeLighting::Update(context, *volume, inputs);
                s_ProbeStepRequested = false;
            }
            if (!ProbeWeighting::InterpolatesTrilinearly(volume->settings.weighting))
            {
                ProbeLighting::EstimateScreenIrradiance(context, volume->settings.weighting, inputs, width, height);
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

void DynamicGI::UpdateEditor(const Math::Camera& camera)
{
    ImGuiLayer::ShowPanel(kDebugPanelName, s_ShowDebugPanel, ShowDebugPanelContent);

    if (FindProbeVolume() == nullptr)
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
            s_SelectedProbe = (probe && probe == GetSelectedProbe()) ? std::nullopt : probe;
    }

    const ProbeContribution::Surface* contribution = GetInspectedContribution();
    if (contribution != nullptr && contribution->hasSurface != 0)
        ProbeVisualization::AnnotateContribution(camera, *contribution, { s_HoveredCorner, s_Settings.inspectLabelScale });
}

void DynamicGI::DrawProbes(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
{
    const ProbeLighting::Volume* volume = FindProbeVolume();
    if (volume == nullptr || !s_Settings.probeSpheres.show)
        return;

    ProbeVisualization::DrawSpheres(context, camera, viewport, scissor, ProbeLighting::GetProbes(), volume->settings.weighting,
        s_Settings.probeSpheres.radiusPerSpacing);
}

void DynamicGI::DrawOverlay(GraphicsContext& context, const Math::Camera& camera)
{
    DrawSelectedDynamicGIVolume(context, camera);

    if (FindProbeVolume() != nullptr && s_Settings.probeSpheres.show && s_Settings.probeSpheres.showOffsets)
        ProbeVisualization::DrawOffsetLines(context, camera, ProbeLighting::GetProbes());

    const std::optional<uint32_t> probe = GetSelectedProbe();
    if (FindProbeVolume() != nullptr && s_Settings.probeSpheres.show && probe)
        ProbeVisualization::DrawDistanceLines(context, camera, ProbeLighting::GetProbes(), *probe, s_Settings.probeSpheres, FindHighlightedRayOfSelectedProbe());

    const ProbeContribution::Surface* contribution = GetInspectedContribution();
    if (contribution != nullptr && contribution->hasSurface != 0)
        ProbeVisualization::DrawContribution(context, camera, ProbeLighting::GetProbes().grid, ProbeContribution::GetSurfaceBuffer(), s_HoveredCorner);
    s_HoveredCorner.reset();
    s_HoveredRay.reset();
}

void DynamicGI::MeasureProbes(const RaytracingScene& scene)
{
    if (IsInspectingPixel() && GetProbeCount(ProbeLighting::GetProbes().grid) != 0)
        ProbeContribution::Measure(FindProbeVolume()->settings.weighting, scene);
    ReplyToMeasuredPixelSelections();
    ProbeReadback::Update(FindProbeOfWatchedRays());
    StepUntilSpike();
    ReplyToCountedProbes();
}
