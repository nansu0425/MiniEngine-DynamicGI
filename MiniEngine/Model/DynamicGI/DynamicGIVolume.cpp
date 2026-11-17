#include "Core/pch.h"
#include "Model/DynamicGI/DynamicGIVolume.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/UI/ImGuiLayer.h"
#include "Model/DynamicGI/DynamicGIVolumeBox.h"
#include "Core/Scene/SettingValues.h"

#include <imgui.h>
#include <algorithm>

using namespace Math;

namespace
{
    constexpr float kMinSize = 0.01f;
    constexpr uint32_t kMaxProbesPerAxis = 64;
    constexpr uint32_t kMaxRaysPerProbe = 1024;
    constexpr float kMaxHysteresis = 0.999f;
    constexpr float kMaxDistanceSharpness = 100.0f;
    constexpr uint32_t kMaxDistanceTexelsPerSide = 32;
    constexpr float kMaxSelfShadowBias = 0.5f;
    constexpr float kMaxChebyshevExponent = 8.0f;
    constexpr uint32_t kMaxRelocationIterations = 16;
    constexpr float kMaxProbeOffset = 0.5f;
    constexpr float kMinPushScale = 1.0f;
    constexpr float kMaxPushScale = 2.0f;

    const char* kVisibilityNames[] = { "None", "Shadow Ray", "Chebyshev" };
    const char* kFacingNormalNames[] = { "Geometry", "Shading" };

    const ImU32 kVolumeColor = IM_COL32(64, 200, 255, 255);
    const ImU32 kSelectedVolumeColor = IM_COL32(255, 128, 16, 255);

    AxisAlignedBox MakeBounds(Vector3 center, Vector3 size)
    {
        const Vector3 halfSize = Max(size, Vector3(kMinSize)) * 0.5f;
        return AxisAlignedBox(center - halfSize, center + halfSize);
    }

    ProbeLighting::Settings ClampSettings(ProbeLighting::Settings settings)
    {
        for (uint32_t& count : settings.probeCounts)
            count = std::max(2u, std::min(count, kMaxProbesPerAxis));
        settings.raysPerProbe = std::max(1u, std::min(settings.raysPerProbe, kMaxRaysPerProbe));
        settings.hysteresis = std::max(0.0f, std::min(settings.hysteresis, kMaxHysteresis));
        settings.distanceSharpness = std::max(1.0f, std::min(settings.distanceSharpness, kMaxDistanceSharpness));
        settings.distanceTexelsPerSide = std::max(1u, std::min(settings.distanceTexelsPerSide, kMaxDistanceTexelsPerSide));
        settings.backFaceDistanceScale = std::max(0.0f, std::min(settings.backFaceDistanceScale, 1.0f));
        ProbeWeighting::Settings& weighting = settings.weighting;
        weighting.selfShadowBias = std::max(0.0f, std::min(weighting.selfShadowBias, kMaxSelfShadowBias));
        weighting.chebyshevExponent = std::max(1.0f, std::min(weighting.chebyshevExponent, kMaxChebyshevExponent));
        weighting.weightCrushThreshold = std::max(0.0f, std::min(weighting.weightCrushThreshold, 1.0f));
        weighting.minFacingWeight = std::max(0.0f, std::min(weighting.minFacingWeight, 1.0f));
        weighting.maxBackFaceRatio = std::max(0.0f, std::min(weighting.maxBackFaceRatio, 1.0f));
        ProbeRelocation::Settings& relocation = settings.relocation;
        relocation.iterations = std::max(1u, std::min(relocation.iterations, kMaxRelocationIterations));
        relocation.maxOffset = std::max(0.0f, std::min(relocation.maxOffset, kMaxProbeOffset));
        relocation.maxPushScale = std::max(kMinPushScale, std::min(relocation.maxPushScale, kMaxPushScale));
        relocation.minPushScale = std::max(kMinPushScale, std::min(relocation.minPushScale, relocation.maxPushScale));
        return settings;
    }

    bool ReadVector(const IniFile::Values& values, const char* key, Vector3& vector)
    {
        const auto text = values.find(key);
        return text != values.end() && SettingValues::ReadVector(text->second, vector);
    }

    void ShowBounds(DynamicGIVolumeComponent& volume)
    {
        ProbeLighting::Volume edited = volume.GetVolume();
        const Vector3 center = edited.bounds.GetCenter();
        const Vector3 size = edited.bounds.GetDimensions();
        float centerValues[3] = { center.GetX(), center.GetY(), center.GetZ() };
        float sizeValues[3] = { size.GetX(), size.GetY(), size.GetZ() };
        const bool movedCenter = ImGui::DragFloat3("Position", centerValues, 1.0f);
        ImGui::SetItemTooltip("Center of the volume in world units. The translate gizmo (W) moves it too.");
        const bool resized = ImGui::DragFloat3("Size", sizeValues, 1.0f, kMinSize, FLT_MAX);
        ImGui::SetItemTooltip("Extent of the volume along the world axes. The scale gizmo (R) resizes it too. "
            "The probe counts stay, so the probe spacing follows the size.");
        if (movedCenter || resized)
        {
            edited.bounds = MakeBounds(Vector3(centerValues[0], centerValues[1], centerValues[2]), Vector3(sizeValues[0], sizeValues[1], sizeValues[2]));
            volume.SetVolume(edited);
        }
        if (ImGui::Button("Fit to Model"))
            volume.FitToModel();
        ImGui::SetItemTooltip("Sets the volume to the bounds of the model.");
    }

    void ShowProbeSettings(ProbeLighting::Settings& probe)
    {
        const uint32_t step = 1;
        const char* axisNames[] = { "Probes X", "Probes Y", "Probes Z" };
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            ImGui::InputScalar(axisNames[axis], ImGuiDataType_U32, &probe.probeCounts[axis], &step);
            ImGui::SetItemTooltip("Probes along this axis of a uniform grid over the volume. Each probe sits at the center of its cell.");
        }
        ImGui::InputScalar("Rays per Probe", ImGuiDataType_U32, &probe.raysPerProbe, &step);
        ImGui::SliderFloat("Hysteresis", &probe.hysteresis, 0.0f, kMaxHysteresis);
        ImGui::SetItemTooltip("How much of the old irradiance each update keeps.");
        ImGui::SliderFloat("Distance Sharpness", &probe.distanceSharpness, 1.0f, kMaxDistanceSharpness);
        ImGui::SetItemTooltip("Exponent on the cosine between a distance texel and a ray that weights the ray's distance in the texel. "
            "Higher values keep only the rays close to the texel direction. The irradiance keeps the cosine itself.");
        ImGui::InputScalar("Distance Texels per Side", ImGuiDataType_U32, &probe.distanceTexelsPerSide, &step);
        ImGui::SetItemTooltip("Texels along each side of a probe's octahedral distance map. The irradiance map keeps 8.");
        ImGui::SliderFloat("Back Face Distance Scale", &probe.backFaceDistanceScale, 0.0f, 1.0f);
        ImGui::SetItemTooltip("A probe ray that first hits the back face of a single-sided material records no radiance "
            "and this fraction of the distance it hit, so the points beyond that face see the probe as occluded.");
    }

    void ShowWeightingSettings(ProbeWeighting::Settings& weighting)
    {
        int visibility = (int)weighting.visibility;
        if (ImGui::Combo("Visibility", &visibility, kVisibilityNames, IM_ARRAYSIZE(kVisibilityNames)))
            weighting.visibility = (ProbeWeighting::Visibility)visibility;
        ImGui::SetItemTooltip("Which of the probes around a point the interpolation keeps. None keeps all of them. "
            "Shadow Ray traces a ray from the point to each probe and keeps the probes it reaches. "
            "Chebyshev weights each probe by how likely the distances it stored toward the point leave the point visible.");
        ImGui::SliderFloat("Self Shadow Bias", &weighting.selfShadowBias, 0.0f, kMaxSelfShadowBias);
        ImGui::SetItemTooltip("While Visibility is Chebyshev, compares the distances the probes stored with a point this far off the surface "
            "along its geometry normal, as a fraction of the shortest probe spacing, instead of the surface point. "
            "A probe the surface faces then sees the point in front of the distances it stored, and a probe behind the surface beyond them.");
        ImGui::SliderFloat("Chebyshev Exponent", &weighting.chebyshevExponent, 1.0f, kMaxChebyshevExponent);
        ImGui::SetItemTooltip("While Visibility is Chebyshev, raises the visibility weight of each probe to this power. "
            "A point within the distances a probe stored keeps the weight 1, and a point beyond them loses weight faster than with 1.");
        ImGui::SliderFloat("Weight Crush Threshold", &weighting.weightCrushThreshold, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Shrinks the weight of a probe below this threshold, before the trilinear weight, by its square over the threshold's square. "
            "The weight is the product of the classification, visibility, and facing weights. "
            "A probe these weights mark as hidden then gives far less than the probes they keep, even after the weights are normalized. 0 turns it off.");
        ImGui::Checkbox("Facing Weight", &weighting.facing);
        ImGui::SetItemTooltip("Weights each probe by how far it lies in front of the surface, along the normal Facing Normal picks, "
            "so the probes behind a thin wall or cloth give less than the ones the surface faces.");
        ImGui::SliderFloat("Min Facing Weight", &weighting.minFacingWeight, 0.0f, 1.0f);
        ImGui::SetItemTooltip("The facing weight of a probe right behind the surface. A probe the surface faces head-on gets 1 more than this.");
        int facingNormal = (int)weighting.facingNormal;
        if (ImGui::Combo("Facing Normal", &facingNormal, kFacingNormalNames, IM_ARRAYSIZE(kFacingNormalNames)))
            weighting.facingNormal = (ProbeWeighting::FacingNormal)facingNormal;
        ImGui::SetItemTooltip("The normal the facing weight measures the side of a probe against. "
            "Geometry is the triangle's normal, constant across each face. Shading is the interpolated vertex normal with the normal map applied.");
        ImGui::Checkbox("Probe Classification", &weighting.classification);
        ImGui::SetItemTooltip("Leaves out of the interpolation the probes whose rays hit the back faces of single-sided materials more often "
            "than Max Back Face Ratio, such as the probes under a floor or inside a wall. "
            "A point with no probe left around it gets no indirect light.");
        ImGui::SliderFloat("Max Back Face Ratio", &weighting.maxBackFaceRatio, 0.0f, 1.0f);
        ImGui::SetItemTooltip("The largest fraction of a probe's rays, averaged over the updates, that may hit back faces "
            "for the probe to stay in the interpolation. Probe Relocation counts a probe with more as inside geometry too.");
    }

    void ShowRelocationSettings(ProbeRelocation::Settings& relocation)
    {
        ImGui::Checkbox("Probe Relocation", &relocation.enabled);
        ImGui::SetItemTooltip("When the probes are cleared, moves each probe whose rays hit back faces more often than Max Back Face Ratio "
            "through the closest back face it hit, out of the geometry it is in. Changing a setting here or Max Back Face Ratio clears the probes.");
        const uint32_t step = 1;
        ImGui::InputScalar("Relocation Iterations", ImGuiDataType_U32, &relocation.iterations, &step);
        ImGui::SetItemTooltip("How many times each probe traces its rays again from where it is and moves if it is still inside geometry.");
        ImGui::SliderFloat("Max Probe Offset", &relocation.maxOffset, 0.0f, kMaxProbeOffset);
        ImGui::SetItemTooltip("Farthest a probe may move from its grid position along each axis, as a fraction of the shortest probe spacing. "
            "A probe that cannot get out of the geometry within it stays.");
        ImGui::SliderFloat("Back Face Push Max Scale", &relocation.maxPushScale, kMinPushScale, kMaxPushScale);
        ImGui::SetItemTooltip("The move tries the vector to the closest back face times scales from this down to Back Face Push Min Scale "
            "and takes the first that stays within Max Probe Offset. 1 puts the probe on the face, 2 as far beyond it as it was inside.");
        ImGui::SliderFloat("Back Face Push Min Scale", &relocation.minPushScale, kMinPushScale, kMaxPushScale);
        ImGui::SetItemTooltip("The smallest scale the move tries. Above 1 keeps the probe off the face it passed, "
            "and leaves more probes where they are when Max Probe Offset is tight.");
    }
}

DynamicGIVolumeComponent::DynamicGIVolumeComponent(std::function<AxisAlignedBox()> findModelBounds)
    : Component({ "Position", "Size", "Probes X", "Probes Y", "Probes Z", "Rays per Probe", "Hysteresis", "Distance Sharpness",
        "Distance Texels per Side", "Back Face Distance Scale", "Visibility", "Self Shadow Bias", "Chebyshev Exponent",
        "Weight Crush Threshold", "Facing Weight", "Min Facing Weight", "Facing Normal", "Probe Classification", "Max Back Face Ratio",
        "Probe Relocation", "Relocation Iterations", "Max Probe Offset", "Back Face Push Max Scale", "Back Face Push Min Scale" }),
    m_FindModelBounds(std::move(findModelBounds))
{
}

void DynamicGIVolumeComponent::SetVolume(const ProbeLighting::Volume& volume)
{
    m_Volume.bounds = MakeBounds(volume.bounds.GetCenter(), volume.bounds.GetDimensions());
    m_Volume.settings = ClampSettings(volume.settings);
}

void DynamicGIVolumeComponent::FitToModel()
{
    const AxisAlignedBox model = m_FindModelBounds();
    m_Volume.bounds = MakeBounds(model.GetCenter(), model.GetDimensions());
}

void DynamicGIVolumeComponent::SaveSettings(IniFile::Values& values) const
{
    values["Position"] = SettingValues::FormatVector(m_Volume.bounds.GetCenter());
    values["Size"] = SettingValues::FormatVector(m_Volume.bounds.GetDimensions());

    const ProbeLighting::Settings& probe = m_Volume.settings;
    values["Probes X"] = std::to_string(probe.probeCounts[0]);
    values["Probes Y"] = std::to_string(probe.probeCounts[1]);
    values["Probes Z"] = std::to_string(probe.probeCounts[2]);
    values["Rays per Probe"] = std::to_string(probe.raysPerProbe);
    values["Hysteresis"] = MeasurementConditions::FormatFloat(probe.hysteresis);
    values["Distance Sharpness"] = MeasurementConditions::FormatFloat(probe.distanceSharpness);
    values["Distance Texels per Side"] = std::to_string(probe.distanceTexelsPerSide);
    values["Back Face Distance Scale"] = MeasurementConditions::FormatFloat(probe.backFaceDistanceScale);
    values["Visibility"] = kVisibilityNames[(int)probe.weighting.visibility];
    values["Self Shadow Bias"] = MeasurementConditions::FormatFloat(probe.weighting.selfShadowBias);
    values["Chebyshev Exponent"] = MeasurementConditions::FormatFloat(probe.weighting.chebyshevExponent);
    values["Weight Crush Threshold"] = MeasurementConditions::FormatFloat(probe.weighting.weightCrushThreshold);
    values["Facing Weight"] = SettingValues::FormatFlag(probe.weighting.facing);
    values["Min Facing Weight"] = MeasurementConditions::FormatFloat(probe.weighting.minFacingWeight);
    values["Facing Normal"] = kFacingNormalNames[(int)probe.weighting.facingNormal];
    values["Probe Classification"] = SettingValues::FormatFlag(probe.weighting.classification);
    values["Max Back Face Ratio"] = MeasurementConditions::FormatFloat(probe.weighting.maxBackFaceRatio);
    values["Probe Relocation"] = SettingValues::FormatFlag(probe.relocation.enabled);
    values["Relocation Iterations"] = std::to_string(probe.relocation.iterations);
    values["Max Probe Offset"] = MeasurementConditions::FormatFloat(probe.relocation.maxOffset);
    values["Back Face Push Max Scale"] = MeasurementConditions::FormatFloat(probe.relocation.maxPushScale);
    values["Back Face Push Min Scale"] = MeasurementConditions::FormatFloat(probe.relocation.minPushScale);
}

void DynamicGIVolumeComponent::LoadSettings(const IniFile::Values& values)
{
    const AxisAlignedBox model = m_FindModelBounds();
    Vector3 center = model.GetCenter();
    Vector3 size = model.GetDimensions();
    ReadVector(values, "Position", center);
    ReadVector(values, "Size", size);

    ProbeLighting::Settings probe;
    SettingValues::ReadUInt(values, "Probes X", probe.probeCounts[0]);
    SettingValues::ReadUInt(values, "Probes Y", probe.probeCounts[1]);
    SettingValues::ReadUInt(values, "Probes Z", probe.probeCounts[2]);
    SettingValues::ReadUInt(values, "Rays per Probe", probe.raysPerProbe);
    SettingValues::ReadFloat(values, "Hysteresis", probe.hysteresis);
    SettingValues::ReadFloat(values, "Distance Sharpness", probe.distanceSharpness);
    SettingValues::ReadUInt(values, "Distance Texels per Side", probe.distanceTexelsPerSide);
    SettingValues::ReadFloat(values, "Back Face Distance Scale", probe.backFaceDistanceScale);
    SettingValues::ReadName(values, "Visibility", kVisibilityNames, probe.weighting.visibility);
    SettingValues::ReadFloat(values, "Self Shadow Bias", probe.weighting.selfShadowBias);
    SettingValues::ReadFloat(values, "Chebyshev Exponent", probe.weighting.chebyshevExponent);
    SettingValues::ReadFloat(values, "Weight Crush Threshold", probe.weighting.weightCrushThreshold);
    SettingValues::ReadFlag(values, "Facing Weight", probe.weighting.facing);
    SettingValues::ReadFloat(values, "Min Facing Weight", probe.weighting.minFacingWeight);
    SettingValues::ReadName(values, "Facing Normal", kFacingNormalNames, probe.weighting.facingNormal);
    SettingValues::ReadFlag(values, "Probe Classification", probe.weighting.classification);
    SettingValues::ReadFloat(values, "Max Back Face Ratio", probe.weighting.maxBackFaceRatio);
    SettingValues::ReadFlag(values, "Probe Relocation", probe.relocation.enabled);
    SettingValues::ReadUInt(values, "Relocation Iterations", probe.relocation.iterations);
    SettingValues::ReadFloat(values, "Max Probe Offset", probe.relocation.maxOffset);
    SettingValues::ReadFloat(values, "Back Face Push Max Scale", probe.relocation.maxPushScale);
    SettingValues::ReadFloat(values, "Back Face Push Min Scale", probe.relocation.minPushScale);

    SetVolume({ MakeBounds(center, size), probe });
}

Vector3 DynamicGIVolumeIcon::GetPosition() const
{
    return m_Volume.GetVolume().bounds.GetCenter();
}

void DynamicGIVolumeIcon::SetPosition(Vector3 position)
{
    ProbeLighting::Volume volume = m_Volume.GetVolume();
    volume.bounds = MakeBounds(position, volume.bounds.GetDimensions());
    m_Volume.SetVolume(volume);
}

void DynamicGIVolumeIcon::Draw(const Camera& camera, bool selected) const
{
    ImVec2 icon;
    if (!SceneEditor::ProjectToScene(camera, GetPosition(), icon))
        return;
    const float radius = SceneEditor::GetIconRadius();
    const float thickness = radius * 0.15f;
    const float half = radius * 0.6f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(ImVec2(icon.x - half, icon.y - half), ImVec2(icon.x + half, icon.y + half), kVolumeColor, 0.0f, 0, thickness);
    drawList->AddCircleFilled(icon, radius * 0.2f, kVolumeColor);
    if (selected)
        drawList->AddCircle(icon, radius * 1.2f, kSelectedVolumeColor, 0, thickness);
}

void DynamicGIVolumeIcon::Scale(const Camera& camera)
{
    ProbeLighting::Volume volume = m_Volume.GetVolume();
    Matrix4 transform(Matrix3::MakeScale(volume.bounds.GetDimensions()), volume.bounds.GetCenter());
    if (!SceneEditor::Manipulate(camera, ImGuizmo::SCALE, ImGuizmo::LOCAL, transform))
        return;
    const Matrix3 scaled = transform.Get3x3();
    volume.bounds = MakeBounds(volume.bounds.GetCenter(), Vector3(Length(scaled.GetX()), Length(scaled.GetY()), Length(scaled.GetZ())));
    m_Volume.SetVolume(volume);
}

void AssembleDynamicGIVolume(Scene::Object& object, std::function<AxisAlignedBox()> findModelBounds)
{
    DynamicGIVolumeComponent& volume = object.AddComponent<DynamicGIVolumeComponent>(std::move(findModelBounds));
    object.AddComponent<DynamicGIVolumeIcon>(volume);
}

void ShowDynamicGIVolumeDetails(DynamicGIVolumeComponent& volume)
{
    ImGui::SeparatorText("Bounds");
    ShowBounds(volume);

    ProbeLighting::Volume edited = volume.GetVolume();
    ImGui::SeparatorText("Probes");
    ShowProbeSettings(edited.settings);
    ImGui::SeparatorText("Weighting");
    ShowWeightingSettings(edited.settings.weighting);
    ImGui::SeparatorText("Probe Relocation");
    ShowRelocationSettings(edited.settings.relocation);
    volume.SetVolume(edited);
}

void DrawSelectedDynamicGIVolume(GraphicsContext& context, const Camera& camera)
{
    const Scene::Object* selected = SceneEditor::GetSelectedObject();
    const DynamicGIVolumeComponent* volume = selected != nullptr ? selected->FindComponent<DynamicGIVolumeComponent>() : nullptr;
    if (volume != nullptr && ImGuiLayer::IsVisible())
        DynamicGIVolumeBox::Draw(context, camera, volume->GetVolume().bounds);
}

const DynamicGIVolumeComponent* FindDynamicGIVolume(void)
{
    for (const Scene::Object* object : Scene::GetObjects())
    {
        if (const DynamicGIVolumeComponent* volume = object->FindComponent<DynamicGIVolumeComponent>())
            return volume;
    }
    return nullptr;
}
