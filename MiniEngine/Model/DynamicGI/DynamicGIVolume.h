#pragma once

#include "Core/Editor/SceneEditor.h"
#include "Core/Scene/Scene.h"
#include "Model/DynamicGI/ProbeLighting.h"

#include <functional>

class GraphicsContext;

constexpr char kDynamicGIVolumeName[] = "Dynamic GI Volume";

class DynamicGIVolumeComponent : public Scene::Component
{
public:
    explicit DynamicGIVolumeComponent(std::function<Math::AxisAlignedBox()> findModelBounds);

    const ProbeLighting::Volume& GetVolume() const { return m_Volume; }
    void SetVolume(const ProbeLighting::Volume& volume);
    void FitToModel();

    virtual void SaveSettings(IniFile::Values& values) const override;
    virtual void LoadSettings(const IniFile::Values& values) override;

private:
    std::function<Math::AxisAlignedBox()> m_FindModelBounds;
    ProbeLighting::Volume m_Volume;
};

class DynamicGIVolumeIcon : public SceneEditor::IconComponent
{
public:
    explicit DynamicGIVolumeIcon(DynamicGIVolumeComponent& volume) : m_Volume(volume) {}

    virtual Math::Vector3 GetPosition() const override;
    virtual void SetPosition(Math::Vector3 position) override;
    virtual void Draw(const Math::Camera& camera, bool selected) const override;
    virtual void Rotate(const Math::Camera&, ImGuizmo::MODE) override {}
    virtual void Scale(const Math::Camera& camera) override;

private:
    DynamicGIVolumeComponent& m_Volume;
};

void AssembleDynamicGIVolume(Scene::Object& object, std::function<Math::AxisAlignedBox()> findModelBounds);
void ShowDynamicGIVolumeDetails(DynamicGIVolumeComponent& volume);
void DrawSelectedDynamicGIVolume(GraphicsContext& context, const Math::Camera& camera);
const DynamicGIVolumeComponent* FindDynamicGIVolume(void);
