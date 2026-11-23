//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard
//

#include "Core/App/GameCore.h"
#include "Core/Camera/CameraController.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Camera/Camera.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Effects/TemporalEffects/TemporalEffects.h"
#include "Core/Effects/MotionBlur/MotionBlur.h"
#include "Core/Effects/DepthOfField/DepthOfField.h"
#include "Core/Effects/PostEffects/PostEffects.h"
#include "Core/Effects/LinearDepth/LinearDepth.h"
#include "Core/Effects/FXAA/FXAA.h"
#include "Core/App/SystemTime.h"
#include "Core/Particles/ParticleEffectManager.h"
#include "Core/App/GameInput.h"
#include "Model/Sponza/SponzaRenderer.h"
#include "Model/Raytracing/RayQuerySmokeTest.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/RaytracingView.h"
#include "Model/Raytracing/RayPick.h"
#include "Model/PathTracer/PathTracer.h"
#include "Model/PathTracer/PathTracerTuning.h"
#include "Model/PathTracer/BrdfTest.h"
#include "Model/DynamicGI/DynamicGI.h"
#include "Model/DynamicGI/DynamicGIVolume.h"
#include "Model/Loader/glTF.h"
#include "Model/Renderer/Renderer.h"
#include "Model/Model.h"
#include "Model/Loader/ModelLoader.h"
#include "Core/Camera/ShadowCamera.h"
#include "Core/Display/Display.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Editor/EditorCameraController.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Scene/Scene.h"
#include "Core/Scene/SettingValues.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Automation/VectorFields.h"
#include "ModelViewer/Light.h"
#include "Core/UI/ImGuiLayer.h"
#include "Core/Utility/IniFile.h"

#include <algorithm>
#include <map>

// Dynamic GI 프로젝트에선 legacy path가 아닌 glTF path를 사용한다.
// legacy는 Blinn-Phong, glTF는 PBR shading
// Skinning/animation은 glTF path에만 있음
// #define LEGACY_RENDERER

using namespace GameCore;
using namespace Math;
using namespace Graphics;
using namespace std;

using Renderer::MeshSorter;

// ray가 아무 면도 만나지 않을 때 받는 빛. 무한히 먼 sky가 모든 방향에서 같은 radiance를 낸다.
// ray tracing으로 간접광을 구하는 곳 (path tracer, 앞으로의 DDGI)이 miss에서 이 radiance를 쓴다. raster는 이 빛으로 surface를 비추지
// 않고 camera에서 바로 보이는 배경으로만 그린다. raster에는 sky의 visibility가 없어서 surface에 더하면 sky가 가려진 실내까지 밝아지고,
// 그 빛은 DDGI가 구한다. 무한히 멀어 icon을 둘 자리가 없으므로 Scene Hierarchy로만 고른다.
class SkyComponent : public Scene::Component
{
public:
    // Details panel, scene file, editor set 명령이 바꾸는 값
    struct Properties
    {
        // directional light처럼 color는 linear RGB이고 성분마다 0 ~ 1이며, intensity와 곱한 것이 빛이다. intensity는 lux가 아니라
        // radiance (cd/m²)이고 0 이상이다. ray가 받는 값이 radiance이고 glTF의 emissive도 cd/m²다.
        Vector3 color;
        float intensity;
    };

    SkyComponent();

    const Properties& GetProperties() const { return m_Properties; }
    void SetProperties(const Properties& properties) { m_Properties = properties; }

    // 모든 방향에서 같은 radiance (cd/m²)
    Vector3 GetRadiance() const { return m_Properties.color * Scalar(m_Properties.intensity); }

    virtual void SaveSettings(IniFile::Values& values) const override;
    virtual void LoadSettings(const IniFile::Values& values) override;

private:
    static Properties GetDefaultProperties();

    Properties m_Properties;
};

// Scene Hierarchy와 editor 명령이 model을 가리키는 이름이자 scene file의 section 이름
const char kModelName[] = "Model";

// scene에 놓인 glTF model. 스스로 보이므로 icon이 없다. 아직 바꿀 수 있는 설정이 없다.
class ModelComponent : public Scene::Component
{
public:
    explicit ModelComponent(const ModelInstance& instance) : m_Instance(instance) {}

    const ModelInstance& GetInstance() const { return m_Instance; }
    void SetPath(const std::string& path) { m_Path = path; }
    const std::string& GetPath() const { return m_Path; }

private:
    const ModelInstance& m_Instance;
    std::string m_Path;
};

// Scene Hierarchy와 editor 명령이 camera를 가리키는 이름이자 scene file의 section 이름
const char kCameraName[] = "Camera";

// scene 을 보는 editor camera. 사용자가 이 camera로 보므로 icon과 gizmo가 없고, mouse와 keyboard나 Details panel로 옮긴다.
// pose와 세로 FOV는 scene 마다 다르므로 scene file에 저장하고, scene 과 무관한 이동 속도는 EditorCameraController가 user.ini에 남긴다.
class CameraComponent : public Scene::Component
{
public:
    // model은 scene file의 Fit=Model이 화면에 담는 model이다.
    CameraComponent(EditorCameraController& controller, Camera& camera, const ModelInstance& model)
        : Component({ "Fit", "Position", "Heading", "Pitch", "Vertical FOV" }), m_Controller(controller), m_Camera(camera), m_Model(model) {}

    // 원점에서 -z를 본다.
    static EditorCameraController::Pose GetDefaultPose();

    EditorCameraController::Pose GetPose() const { return m_Controller.GetPose(); }
    void SetPose(const EditorCameraController::Pose& pose) { m_Controller.SetPose(pose); }
    // radian. IsVerticalFov인 값만 둔다.
    float GetVerticalFov() const { return m_Camera.GetFOV(); }
    // SetPose처럼 다음 Update를 기다리지 않고 camera에 바로 적용해, raster와 ray가 같은 frame부터 새 FOV로 그리게 한다.
    void SetVerticalFov(float verticalFov) { m_Camera.SetFOV(verticalFov); m_Camera.Update(); }

    virtual void SaveSettings(IniFile::Values& values) const override;
    // scene file의 두 형식을 모두 읽는다. Fit=Model이면 지금 model을 담는 pose로 옮긴다.
    virtual void LoadSettings(const IniFile::Values& values) override;

private:
    EditorCameraController& m_Controller;
    Camera& m_Camera;
    const ModelInstance& m_Model;
};

class ModelViewer : public GameCore::IGameApp
{
public:

    ModelViewer( void ) {}

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;
    virtual void RenderSceneOverlay( GraphicsContext& gfxContext ) override;

    virtual bool RequiresInlineRaytracing() const override { return true; }

private:

    struct PreparedScene;
    // path의 scene file로 scene 을 연다. Scene menu와 scene open 명령이 함께 쓰고, 다른 scene 의 줄을 받은 Restore도 PrepareScene과
    // ShowScene으로 같은 일을 한다. 열 수 없으면 error에 이유를 쓰고 false를 돌려주며 지금 scene 을 그대로 둔다.
    bool OpenScene(const std::wstring& path, std::string& error);
    // scene 을 바꾸지 않고 path의 scene file을 열 수 있는지 본다. 그 model을 처음 열면 load해서 m_Models에 남긴다.
    bool PrepareScene(const std::wstring& path, PreparedScene& scene, std::string& error);
    // PrepareScene이 준비한 scene 으로 바꾼다. camera와 light 같은 object는 scene file의 설정을 받고, path tracer는 처음부터 다시 쌓는다.
    void ShowScene(const PreparedScene& scene);
    // object의 설정을 지금 scene file에 쓴다. camera도 object이므로 지금 pose를 함께 쓴다. 쓸 수 없으면 error에 이유를 쓰고 false를
    // 돌려준다.
    bool SaveScene(std::string& error);
    void ShowSceneMenu(void);
    void RegisterSceneCommand(void);
    // 측정 조건의 줄에 지금 scene 을 담고, 다른 scene 의 줄을 Restore하면 그 scene 을 연다.
    void RegisterSceneConditions(void);
    void ReadSceneConditions(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading);

    // 한 번 연 model을 glTF 경로마다 남겨 다시 쓴다. Renderer::LoadModel은 load할 때마다 scene texture descriptor heap에서
    // descriptor를 할당하고 풀지 않으므로, scene 을 오갈 때마다 load하면 heap이 찬다.
    std::map<std::wstring, std::shared_ptr<const Model>> m_Models;

    Camera m_Camera;
    unique_ptr<EditorCameraController> m_CameraController;
    // component는 Startup이 m_CameraController를 만든 뒤 붙인다. m_CameraController보다 먼저 없어지도록 뒤에 둔다.
    Scene::Object m_CameraObject{ kCameraName };
    // -rebuild 1이면 처음 load하는 model의 .mini를 다시 만든다.
    bool m_ForceRebuild = false;
    // 연 scene. state와 측정 조건의 줄이 scene 을 가리키는 이름이다.
    std::string m_SceneName;

    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT m_MainScissor;

    ModelInstance m_ModelInst;
    Scene::Object m_ModelObject{ kModelName };
    ModelComponent& m_ModelComponent = m_ModelObject.AddComponent<ModelComponent>(m_ModelInst);
    RaytracingScene m_RaytracingScene;
    ShadowCamera m_ShadowCamera;
    Scene::Object m_LightObject{ "Directional Light" };
    LightComponent& m_Light = AssembleLight(m_LightObject);
    Scene::Object m_SkyObject{ "Sky" };
    SkyComponent& m_Sky = m_SkyObject.AddComponent<SkyComponent>();
    Scene::Object m_GIVolumeObject{ kDynamicGIVolumeName };
};

CREATE_APPLICATION( ModelViewer )

// scripts/viewer.py가 이 이름과 형식으로 -scene이 없을 때 여는 scene file의 경로를 읽는다.
const wchar_t kDefaultScene[] = L"Scenes/Sponza.ini";
// Scene menu가 scene file을 찾는 폴더
const wchar_t kScenesFolder[] = L"Scenes/";

// 기본값은 맑은 날 한낮의 두 illuminance의 비로 정한다. 하나는 해가 비추는 면이고, 다른 하나는 해만 가려진 그늘이다.
// 그늘은 하늘의 빛만 받는다고 본다. 실제로는 그늘을 만든 물체가 하늘 일부를 가리고 빛을 반사하기도 하는데, 이 둘은 무시한다.
// Wikipedia "Daylight"의 illuminance 표는 앞의 것을 111,000 lux로, 뒤의 것을 20,000 lux로 든다. 표에는 출처 표기가 없고 값도
// 어림이다. sky의 radiance가 L이면 위를 향한 면은 πL lux를 받는다. 그래서 πL : kDefaultLightIntensity = 20,000 : 111,000이다.
constexpr float kDefaultSkyIntensity = kDefaultLightIntensity / XM_PI * (20000.0f / 111000.0f);

// scene file과 측정 조건의 줄에 세로 FOV가 없을 때의 값 (radian). FOV를 scene 마다 정하기 전의 camera는 늘 upstream Camera가 처음 두는
// XM_PIDIV4 (45°)로 그렸으므로, 그때의 scene file과 줄이 그때와 같은 화면이 된다. scripts/viewer.py가 이 이름과 형식으로 읽는다.
constexpr float kDefaultCameraVerticalFov = 0.785398163f;
static_assert(kDefaultCameraVerticalFov == XM_PIDIV4, "scene files and lines without a vertical FOV must keep the view they had before");

// 0 이하는 보이는 것이 없고, pi 이상은 camera 앞의 반구를 넘어 perspective projection이 정해지지 않는다.
bool IsVerticalFov(float verticalFov)
{
    return verticalFov > 0.0f && verticalFov < XM_PI;
}

SkyComponent::Properties SkyComponent::GetDefaultProperties()
{
    Properties properties;
    properties.color = Vector3(1.0f, 1.0f, 1.0f);
    properties.intensity = kDefaultSkyIntensity;
    return properties;
}

SkyComponent::SkyComponent() : Component({ "Color", "Intensity" }), m_Properties(GetDefaultProperties())
{
}

void SkyComponent::SaveSettings(IniFile::Values& values) const
{
    values["Color"] = SettingValues::FormatVector(m_Properties.color);
    char text[32];
    sprintf_s(text, "%.9g", m_Properties.intensity);
    values["Intensity"] = text;
}

// directional light와 달리 intensity의 inf는 받는다. path tracer가 NaN과 Inf인 sample을 버리는지 볼 때 inf로 둔다.
void SkyComponent::LoadSettings(const IniFile::Values& values)
{
    Properties properties = GetDefaultProperties();
    const auto color = values.find("Color");
    Vector3 readColor;
    if (color != values.end() && SettingValues::ReadVector(color->second, readColor) && IsColor(readColor))
        properties.color = readColor;
    const auto intensity = values.find("Intensity");
    float number = 0.0f;
    if (intensity != values.end() && sscanf_s(intensity->second.c_str(), "%f", &number) == 1 && !std::isnan(number) && number >= 0.0f)
        properties.intensity = number;
    m_Properties = properties;
}

// state의 sky
ControlChannel::Fields DescribeSky(const SkyComponent& sky)
{
    const SkyComponent::Properties& properties = sky.GetProperties();
    return ControlChannel::Fields()
        .Object("color", ControlChannel::Fields()
            .Float("r", properties.color.GetX()).Float("g", properties.color.GetY()).Float("b", properties.color.GetZ()))
        .Float("intensity", properties.intensity);
}

// Sky를 harness의 state로 읽게 한다. 값은 Details panel처럼 editor 명령으로 바꾼다.
void RegisterSky(SkyComponent& sky)
{
    ControlChannel::RegisterState("sky", [&sky] { return DescribeSky(sky); });
}

void ShowSkyDetails(SkyComponent& sky)
{
    SkyComponent::Properties properties = sky.GetProperties();
    float color[3] = { properties.color.GetX(), properties.color.GetY(), properties.color.GetZ() };
    if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_Float))
        properties.color = Min(Max(Vector3(color[0], color[1], color[2]), Vector3(kZero)), Vector3(1.0f, 1.0f, 1.0f));
    ImGui::SetItemTooltip("Linear RGB, each 0 to 1.");
    if (ImGui::DragFloat("Intensity", &properties.intensity, 0.01f, 0.0f, FLT_MAX, "%.3f cd/m2"))
        properties.intensity = std::max(0.0f, properties.intensity);
    ImGui::SetItemTooltip("Radiance of the sky in every direction, in cd/m2. An upward surface open to the whole sky receives "
        "pi times this in lux.");
    sky.SetProperties(properties);
    ImGui::TextDisabled("Rays that hit nothing see this light. The raster does not light surfaces with it and draws it only as the background.");
}

// 아직 바꿀 수 있는 설정이 없어 model의 정보만 표시한다.
void ShowModelDetails(ModelComponent& component)
{
    const ModelInstance& instance = component.GetInstance();
    const Model& model = instance.GetModel();
    const AxisAlignedBox bounds = instance.GetAxisAlignedBox();
    const Vector3 min = bounds.GetMin();
    const Vector3 max = bounds.GetMax();
    ImGui::TextWrapped("glTF: %s", component.GetPath().c_str());
    ImGui::Text("Meshes: %u, nodes: %u", model.m_NumMeshes, model.m_NumNodes);
    ImGui::Text("Bounds min: %.2f, %.2f, %.2f", (float)min.GetX(), (float)min.GetY(), (float)min.GetZ());
    ImGui::Text("Bounds max: %.2f, %.2f, %.2f", (float)max.GetX(), (float)max.GetY(), (float)max.GetZ());
    ImGui::SetItemTooltip("World-space axis-aligned bounds, as state model gives them.");
    ImGui::TextDisabled("Read only. The model has no settings to change yet.");
}

std::string FormatFloat(float value)
{
    char text[32];
    sprintf_s(text, "%.9g", value);
    return text;
}

// 측정 조건의 줄에 담는 값. 같은 화면으로 되돌릴 수 있게 다시 읽으면 같은 float가 되는 자릿수로 쓰고, 줄에서 읽을 때는 앞뒤에 다른
// 글자가 있거나 받을 수 없는 값을 받지 않는다 (MeasurementConditions::Restore).
using MeasurementConditions::ReadValue;

void DescribeCameraConditions(const CameraComponent& camera, MeasurementConditions::Values& values)
{
    const EditorCameraController::Pose pose = camera.GetPose();
    values["camera"] = SettingValues::FormatVector(pose.position);
    values["heading"] = FormatFloat(pose.heading);
    values["pitch"] = FormatFloat(pose.pitch);
    values["vertical fov"] = FormatFloat(camera.GetVerticalFov());
}

// vertical fov가 없는 줄은 FOV를 scene 마다 정하기 전의 줄이고 그때는 늘 kDefaultCameraVerticalFov로 그렸으므로, 그 FOV로 되돌린다.
void ReadCameraConditions(CameraComponent& camera, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    EditorCameraController::Pose pose = camera.GetPose();
    ReadValue(values, "camera", reading, [&pose](const std::string& text) { return SettingValues::ReadVector(text, pose.position); });
    ReadValue(values, "heading", reading, [&pose](const std::string& text) { return SettingValues::ReadNumbers(text, &pose.heading, 1); });
    ReadValue(values, "pitch", reading, [&pose](const std::string& text) { return SettingValues::ReadNumbers(text, &pose.pitch, 1); });
    float verticalFov = kDefaultCameraVerticalFov;
    if (values.count("vertical fov") != 0)
    {
        ReadValue(values, "vertical fov", reading, [&verticalFov](const std::string& text)
        {
            return SettingValues::ReadNumbers(text, &verticalFov, 1) && IsVerticalFov(verticalFov);
        });
    }
    reading.applies.push_back([&camera, pose, verticalFov]
    {
        camera.SetPose(pose);
        camera.SetVerticalFov(verticalFov);
    });
}

// scene의 해상도는 camera ray가 지나는 pixel 격자를 정하지만, UI가 보이면 dock 배치가 정하므로 되돌리지 않고 다르면 알린다.
std::string FormatSceneSize(uint32_t width, uint32_t height)
{
    return std::to_string(width) + "x" + std::to_string(height);
}

void DescribeSceneSize(MeasurementConditions::Values& values)
{
    values["size"] = FormatSceneSize(g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());
}

void ReadSceneSize(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    uint32_t width = 0, height = 0;
    const bool read = ReadValue(values, "size", reading, [&width, &height](const std::string& text)
    {
        const size_t separator = text.find('x');
        return separator != std::string::npos && MeasurementConditions::ReadUInt(text.substr(0, separator), width) &&
            MeasurementConditions::ReadUInt(text.substr(separator + 1), height);
    });
    if (read && (width != g_SceneColorBuffer.GetWidth() || height != g_SceneColorBuffer.GetHeight()))
    {
        reading.notes.push_back("the resolution is " + FormatSceneSize(width, height) + " in the line but " +
            FormatSceneSize(g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight()) + " now. Restore does not change it.");
    }
}

void DescribeTaa(MeasurementConditions::Values& values)
{
    values["taa"] = TemporalEffects::EnableTAA ? "on" : "off";
}

void ReadTaa(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    bool taa = false;
    if (!ReadValue(values, "taa", reading, [&taa](const std::string& text) { taa = text == "on"; return text == "on" || text == "off"; }))
        return;
    if (taa)
        reading.notes.push_back("TAA was on in the line, so its jitter cannot be reproduced.");
    reading.applies.push_back([taa] { TemporalEffects::EnableTAA = taa; });
}

void DescribeSkyConditions(const SkyComponent& sky, MeasurementConditions::Values& values)
{
    const SkyComponent::Properties& properties = sky.GetProperties();
    values["sky color"] = SettingValues::FormatVector(properties.color);
    values["sky intensity"] = FormatFloat(properties.intensity);
}

// intensity는 Sky의 settings처럼 inf도 받는다 (SkyComponent::LoadSettings). /fp:fast에서는 NaN과의 비교가 참일 수 있어 NaN을 따로 본다.
void ReadSkyConditions(SkyComponent& sky, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    SkyComponent::Properties properties = sky.GetProperties();
    ReadValue(values, "sky color", reading, [&properties](const std::string& text) { return SettingValues::ReadVector(text, properties.color) && IsColor(properties.color); });
    ReadValue(values, "sky intensity", reading, [&properties](const std::string& text)
    {
        return MeasurementConditions::ReadFloat(text, properties.intensity) && !std::isnan(properties.intensity) && properties.intensity >= 0.0f;
    });
    reading.applies.push_back([&sky, properties] { sky.SetProperties(properties); });
}

// raster와 path tracer가 그리는 조건을 Log와 capture의 줄에 담고 Restore로 되돌리게 한다. scene 은 ModelViewer::RegisterSceneConditions가,
// path tracer의 설정은 PathTracerTuning이 더한다.
void RegisterMeasurementConditions(CameraComponent& camera, LightComponent& light, SkyComponent& sky)
{
    MeasurementConditions::Register("camera", {
        [&camera](MeasurementConditions::Values& values) { DescribeCameraConditions(camera, values); },
        [&camera](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadCameraConditions(camera, values, reading); },
        { "camera" } });
    MeasurementConditions::Register("size", { DescribeSceneSize, ReadSceneSize, {} });
    MeasurementConditions::Register("taa", { DescribeTaa, ReadTaa, { "settings.Engine Tuning" } });
    MeasurementConditions::Register("light", {
        [&light](MeasurementConditions::Values& values) { DescribeLightConditions(light, values); },
        [&light](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadLightConditions(light, values, reading); },
        { "light" } });
    MeasurementConditions::Register("sky", {
        [&sky](MeasurementConditions::Values& values) { DescribeSkyConditions(sky, values); },
        [&sky](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadSkyConditions(sky, values, reading); },
        { "sky" } });
}

void UnregisterMeasurementConditions(void)
{
    for (const char* group : { "scene", "camera", "size", "taa", "light", "sky" })
        MeasurementConditions::Unregister(group);
}

// state의 camera
ControlChannel::Fields DescribeCamera(const Camera& camera, const EditorCameraController& controller)
{
    const Vector3 position = camera.GetPosition();
    const EditorCameraController::Pose pose = controller.GetPose();
    return ControlChannel::Fields()
        .Text("controller", "editor")
        .Float("x", position.GetX())
        .Float("y", position.GetY())
        .Float("z", position.GetZ())
        .Float("heading", pose.heading)
        .Float("pitch", pose.pitch)
        .Float("move_speed", controller.GetMoveSpeed())
        .Object("forward", ControlChannel::DescribeVector(camera.GetForwardVec()))
        .Float("vertical_fov", camera.GetFOV())
        .Float("aspect", 1.0f / camera.GetAspectRatio())
        .Float("near", camera.GetNearClip())
        .Float("far", camera.GetFarClip());
}

ControlChannel::Fields DescribeModel(const std::string& path, const ModelInstance& model)
{
    const AxisAlignedBox bounds = model.GetAxisAlignedBox();
    return ControlChannel::Fields()
        .Text("path", path)
        .Object("bounds", ControlChannel::Fields().Object("min", ControlChannel::DescribeVector(bounds.GetMin())).Object("max", ControlChannel::DescribeVector(bounds.GetMax())));
}

// harness가 camera를 숫자로 읽고 정한다.
void RegisterPoseCommand(EditorCameraController& controller)
{
    ControlChannel::SetStatus("camera", "editor");
    ControlChannel::Register("pose", "pose | pose <x> <y> <z> <heading> <pitch> | pose look-at <x> <y> <z>: the editor camera, "
        "in world units and radians as in state camera. look-at keeps the position and turns the camera toward the point. "
        "Looking straight up or down keeps the heading. The camera is the Camera of the Scene Hierarchy, so scene save writes this pose "
        "to the scene file", [&controller](const std::string& arguments, const ControlChannel::Reply& reply)
    {
        float x, y, z, heading, pitch;
        const std::string usage = "usage: pose <x> <y> <z> <heading> <pitch> | pose look-at <x> <y> <z>";
        if (arguments.compare(0, 8, "look-at ") == 0)
        {
            char rest;
            if (sscanf_s(arguments.c_str() + 8, "%f %f %f %c", &x, &y, &z, &rest, 1) != 3)
            {
                reply.Error(usage);
                return;
            }
            const Vector3 target(x, y, z);
            if (LengthSquare(target - controller.GetPose().position) == 0.0f)
            {
                reply.Error("the point is where the camera is, so there is no direction to look in");
                return;
            }
            controller.LookAt(target);
        }
        else if (!arguments.empty())
        {
            if (sscanf_s(arguments.c_str(), "%f %f %f %f %f", &x, &y, &z, &heading, &pitch) != 5)
            {
                reply.Error(usage);
                return;
            }
            controller.SetPose({ Vector3(x, y, z), heading, pitch });
        }

        const EditorCameraController::Pose pose = controller.GetPose();
        reply.Ok(ControlChannel::Fields()
            .Float("x", pose.position.GetX())
            .Float("y", pose.position.GetY())
            .Float("z", pose.position.GetZ())
            .Float("heading", pose.heading)
            .Float("pitch", pose.pitch)
            .Object("state", ControlChannel::GetState("camera")));
    });
}

// scene file의 [Camera] section 을 읽은 것. fitModel이면 pose를 쓰지 않는다.
struct CameraSection
{
    bool fitModel = false;
    EditorCameraController::Pose pose = { Vector3(kZero), 0.0f, 0.0f };
    float verticalFov = kDefaultCameraVerticalFov;
};

// scene 은 scene file 하나로 연다. 형식은 IniFile이고 key는 아래와 같다. 실행 중에는 Scene menu와 scene open 명령으로 다른 scene 을 연다.
// section은 이름이 같은 Scene Hierarchy의 object (Model, Camera, Directional Light, Sky) 의 것이다. Scene menu의 Save와 scene save
// 명령은 모든 object의 지금 설정을 object의 section에 쓴다. 설정의 key는 object의 component가 가진 setting key다 (Scene::Component).
//   [Model]  아래 key는 scene 을 열 때 model을 load하고 크기를 정하는 데 읽는다. Model object의 설정이 아니므로 Save가 쓰지 않는다.
//            glTF: scene file이 있는 폴더 기준의 glTF 경로
//            Scale: glTF 좌표에 곱해 world 좌표로 바꾸는 배율. 다음의 Radius와 둘 중 하나만 둔다.
//            Radius: model의 bounding sphere 반지름이 이 world 길이가 되는 배율 (ModelInstance::Resize)
//   [Directional Light], [Sky] section이나 key가 없으면 기본값이다.
//   [Camera] Position=<x>,<y>,<z>, Heading, Pitch: camera의 pose. 단위는 state camera와 pose 명령과 같다.
//            Fit=Model: 위 셋 대신 둘 수 있다. bounding sphere 중심의 +z 쪽 반지름의 2배와 near clip을 더한 거리에서 -z를 본다.
//            예전 -model의 OrbitCamera가 처음 그린 화면과 bit 단위로 같다. Save는 Fit=Model 줄을 지우고 지금 pose를 위 셋으로 쓴다.
//            두 형식 중 하나가 없으면 scene file을 열지 않는다.
//            Vertical FOV: camera의 세로 FOV (radian). 0보다 크고 pi보다 작다. 없으면 kDefaultCameraVerticalFov이고, 받을 수 없는 값이면
//            scene file을 열지 않는다.
// scene 을 열면 camera는 scene file의 pose와 세로 FOV로 시작한다. 다른 scene 의 줄을 Restore하면 줄의 camera를 쓴다. user.ini는
// camera의 이동 속도만 남긴다.
struct SceneFile
{
    // scale과 radius 중 0이 아닌 하나만 쓴다.
    std::wstring gltfPath;
    float scale = 0.0f;
    float radius = 0.0f;
    IniFile::Sections sections;
};

// scene file 폴더 기준의 경로를 working directory 기준으로 바꾼다. 앞에 붙은 "../"마다 폴더를 한 단계 올라간다.
std::wstring ResolveFromSceneFolder(const std::wstring& scenePath, std::wstring path)
{
    std::wstring folder = Utility::GetBasePath(scenePath);
    while (path.compare(0, 3, L"../") == 0 && !folder.empty() && folder != L"../" &&
        (folder.size() < 4 || folder.compare(folder.size() - 4, 4, L"/../") != 0))
    {
        folder = Utility::GetBasePath(folder.substr(0, folder.size() - 1));
        path = path.substr(3);
    }
    return folder + path;
}

const std::string* FindValue(const IniFile::Values& values, const char* key)
{
    const auto value = values.find(key);
    return value == values.end() ? nullptr : &value->second;
}

// values가 Fit=Model과 Position, Heading, Pitch 중 한 형식만 가지고, Vertical FOV가 없거나 IsVerticalFov인 숫자일 때 true를 돌려준다.
// Fit가 Model이 아니면 false다.
bool ReadCameraSection(const IniFile::Values& values, CameraSection& camera)
{
    const std::string* fit = FindValue(values, "Fit");
    const std::string* position = FindValue(values, "Position");
    const std::string* heading = FindValue(values, "Heading");
    const std::string* pitch = FindValue(values, "Pitch");
    const std::string* verticalFov = FindValue(values, "Vertical FOV");
    camera.fitModel = fit != nullptr && *fit == "Model";
    const bool hasPose = position != nullptr && heading != nullptr && pitch != nullptr && SettingValues::ReadVector(*position, camera.pose.position) &&
        SettingValues::ReadNumbers(*heading, &camera.pose.heading, 1) && SettingValues::ReadNumbers(*pitch, &camera.pose.pitch, 1);
    const bool readsVerticalFov = verticalFov == nullptr || (SettingValues::ReadNumbers(*verticalFov, &camera.verticalFov, 1) && IsVerticalFov(camera.verticalFov));
    return readsVerticalFov && camera.fitModel != hasPose && (fit == nullptr || camera.fitModel);
}

// 받을 수 없는 값이 있으면 error에 이유를 쓰고 false를 돌려준다.
bool ReadSceneFile(const std::wstring& path, SceneFile& scene, std::string& error)
{
    IniFile::Sections& sections = scene.sections;
    sections = IniFile::Read(path);
    const IniFile::Values& model = sections[kModelName];

    const std::string* gltf = FindValue(model, "glTF");
    if (gltf == nullptr)
    {
        error = "has no [Model] glTF, or cannot be read";
        return false;
    }
    scene.gltfPath = ResolveFromSceneFolder(path, Utility::UTF8ToWideString(*gltf));

    const std::string* scale = FindValue(model, "Scale");
    const std::string* radius = FindValue(model, "Radius");
    if ((scale == nullptr) == (radius == nullptr) || !SettingValues::ReadNumbers(scale != nullptr ? *scale : *radius, scale != nullptr ? &scene.scale : &scene.radius, 1) ||
        scene.scale < 0.0f || scene.radius < 0.0f || scene.scale + scene.radius == 0.0f)
    {
        error = "needs either [Model] Scale or Radius as a number above 0";
        return false;
    }

    // [Camera] section 은 scene 을 열 때 CameraComponent::LoadSettings가 받는다. 여기서는 열기 전에 받을 수 있는지만 본다.
    CameraSection camera;
    if (!ReadCameraSection(sections[kCameraName], camera))
    {
        error = "needs either [Camera] Fit=Model or Position=<x>,<y>,<z> with Heading and Pitch, and Vertical FOV, if any, in radians above 0 and below pi";
        return false;
    }
    return true;
}

void ScaleModel(ModelInstance& model, const SceneFile& scene)
{
    if (scene.scale > 0.0f)
        model.SetScale(scene.scale);
    else
        model.Resize(scene.radius);
}

EditorCameraController::Pose CameraComponent::GetDefaultPose()
{
    return { Vector3(kZero), 0.0f, 0.0f };
}

void CameraComponent::SaveSettings(IniFile::Values& values) const
{
    const EditorCameraController::Pose pose = m_Controller.GetPose();
    values["Position"] = SettingValues::FormatVector(pose.position);
    values["Heading"] = FormatFloat(pose.heading);
    values["Pitch"] = FormatFloat(pose.pitch);
    values["Vertical FOV"] = FormatFloat(GetVerticalFov());
}

void CameraComponent::LoadSettings(const IniFile::Values& values)
{
    CameraSection camera;
    if (!ReadCameraSection(values, camera))
    {
        m_Controller.SetPose(GetDefaultPose());
        SetVerticalFov(kDefaultCameraVerticalFov);
        return;
    }
    SetVerticalFov(camera.verticalFov);
    if (!camera.fitModel)
    {
        m_Controller.SetPose(camera.pose);
        return;
    }
    const BoundingSphere sphere = m_Model.GetBoundingSphere();
    m_Controller.SetPose({ sphere.GetCenter() + Vector3(0.0f, 0.0f, sphere.GetRadius() * 2.0f + m_Controller.GetCameraNearClip()), 0.0f, 0.0f });
}

void ShowCameraDetails(CameraComponent& camera)
{
    EditorCameraController::Pose pose = camera.GetPose();
    float position[3] = { pose.position.GetX(), pose.position.GetY(), pose.position.GetZ() };
    bool changed = ImGui::DragFloat3("Position", position, 1.0f, 0.0f, 0.0f, "%.1f");
    ImGui::SetItemTooltip("World units, as state camera and the pose command give it.");
    changed |= ImGui::SliderAngle("Heading", &pose.heading, -180.0f, 180.0f, "%.2f deg");
    ImGui::SetItemTooltip("Turn about world +y. 0 looks along -z, and a larger angle turns toward -x. The scene file keeps it in radians.");
    changed |= ImGui::SliderAngle("Pitch", &pose.pitch, -90.0f, 90.0f, "%.2f deg");
    ImGui::SetItemTooltip("Up from the horizon. The scene file keeps it in radians.");
    if (changed)
    {
        pose.position = Vector3(position[0], position[1], position[2]);
        camera.SetPose(pose);
    }
    float verticalFov = camera.GetVerticalFov();
    if (ImGui::SliderAngle("Vertical FOV", &verticalFov, 1.0f, 179.0f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp))
        camera.SetVerticalFov(verticalFov);
    ImGui::SetItemTooltip("Vertical field of view. The scene file keeps it in radians.");
    ImGui::TextDisabled("You look through the camera, so it has no icon or gizmo. Move it with the mouse and keys.");
    ImGui::TextDisabled("Scene > Save writes this pose and field of view. The move speed is not part of the scene and stays in user.ini.");
}

ControlChannel::Fields DescribeScene(const std::string& path)
{
    return ControlChannel::Fields().Text("path", path);
}

// state와 측정 조건의 줄이 scene 을 가리키는 이름. '\'로 적은 경로도 '/'로 적은 줄과 같은 scene 이 되게 한다.
std::string GetSceneName(const std::wstring& path)
{
    std::string name = Utility::ToUTF8(path);
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

struct ModelViewer::PreparedScene
{
    std::string name;
    SceneFile file;
    std::shared_ptr<const Model> model;
};

bool ModelViewer::PrepareScene(const std::wstring& path, PreparedScene& scene, std::string& error)
{
    if (!ReadSceneFile(path, scene.file, error))
        return false;
    auto model = m_Models.find(scene.file.gltfPath);
    if (model == m_Models.end())
    {
        std::shared_ptr<const Model> loaded = Renderer::LoadModel(scene.file.gltfPath, m_ForceRebuild);
        if (loaded == nullptr)
        {
            error = "cannot load its glTF " + Utility::ToUTF8(scene.file.gltfPath);
            return false;
        }
        model = m_Models.emplace(scene.file.gltfPath, loaded).first;
    }
    scene.model = model->second;
    scene.name = GetSceneName(path);
    return true;
}

void ModelViewer::ShowScene(const PreparedScene& scene)
{
    // 지난 frame의 command가 아직 지금 model instance의 buffer와 TLAS를 읽을 수 있으므로 GPU가 끝내기를 기다린 뒤에 바꾼다.
    g_CommandManager.IdleGPU();
    // TLAS의 buffer는 처음 Build한 scene 의 instance 수에 맞춰 만들므로 새 scene 에 맞게 다시 만들게 한다.
    m_RaytracingScene.Destroy();
    m_ModelInst = scene.model;
    // camera의 Fit=Model이 새 model을 담도록 object의 설정보다 먼저 크기를 정한다.
    ScaleModel(m_ModelInst, scene.file);
    m_ModelComponent.SetPath(Utility::ToUTF8(scene.file.gltfPath));
    m_SceneName = scene.name;
    Scene::LoadObjectSettings(scene.file.sections);
    PathTracer::Reset();
    DynamicGI::Reset();
}

bool ModelViewer::SaveScene(std::string& error)
{
    if (m_ModelInst.IsNull())
    {
        error = "is not open";
        return false;
    }
    if (!IniFile::WriteSections(Utility::UTF8ToWideString(m_SceneName), Scene::SaveObjectSettings(), Scene::GetAbsentOptionalObjectNames()))
    {
        error = "cannot be written";
        return false;
    }
    return true;
}

bool ModelViewer::OpenScene(const std::wstring& path, std::string& error)
{
    PreparedScene scene;
    if (!PrepareScene(path, scene, error))
        return false;
    ShowScene(scene);
    return true;
}

// scene이 없는 줄은 scene file을 쓰기 전의 줄이고, 그때는 -model로 연 scene 을 Restore할 수 없었으므로 kDefaultScene의 줄로 읽는다.
// 줄의 scene 이 지금 scene 과 다르면 줄의 나머지를 받을 수 있을 때만 그 scene 을 준비해 보고, 열 수 없으면 아무것도 되돌리지 않는다.
void ModelViewer::ReadSceneConditions(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    const auto scene = values.find("scene");
    const std::string lineScene = scene != values.end() ? scene->second : Utility::ToUTF8(kDefaultScene);
    if (lineScene == m_SceneName)
        return;
    reading.preparations.push_back([this, lineScene](MeasurementConditions::Reading& reading)
    {
        const std::shared_ptr<PreparedScene> prepared = std::make_shared<PreparedScene>();
        std::string error;
        if (!PrepareScene(Utility::UTF8ToWideString(lineScene), *prepared, error))
        {
            reading.unread.push_back("scene");
            reading.notes.push_back("the scene file " + lineScene + " " + error + ".");
            return;
        }
        // scene 을 열면 camera와 light 같은 object가 scene file의 설정을 받으므로, 줄의 camera, light, sky가 그 뒤에 들어가도록 다른
        // group보다 먼저 연다.
        reading.applies.insert(reading.applies.begin(), [this, prepared] { ShowScene(*prepared); });
    });
}

void ModelViewer::RegisterSceneConditions(void)
{
    MeasurementConditions::Register("scene", {
        [this](MeasurementConditions::Values& values) { values["scene"] = m_SceneName; },
        [this](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadSceneConditions(values, reading); },
        { "scene", "model" } });
}

// Scene menu가 보이는 scene file. kScenesFolder의 .ini를 이름순으로 둔다.
std::vector<std::wstring> ListSceneFiles(void)
{
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW found;
    const HANDLE find = FindFirstFileW((std::wstring(kScenesFolder) + L"*.ini").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
        return files;
    do
    {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            files.push_back(kScenesFolder + std::wstring(found.cFileName));
    } while (FindNextFileW(find, &found));
    FindClose(find);
    std::sort(files.begin(), files.end());
    return files;
}

// Save는 scene save 명령처럼 camera의 pose와 다른 object의 설정을 지금 scene file에 쓴다. scene file 항목은 지금 scene 에 표시를 두고,
// 누르면 scene open 명령처럼 그 scene 을 연다.
void ModelViewer::ShowSceneMenu(void)
{
    if (ImGui::MenuItem("Save", nullptr, false, !m_ModelInst.IsNull()))
    {
        std::string error;
        if (!SaveScene(error))
            Utility::Printf("Error: the scene file %s %s\n", m_SceneName.c_str(), error.c_str());
    }
    ImGui::SetItemTooltip("Writes the settings of the objects in the Scene Hierarchy, the pose of the camera among them, to %s and leaves its "
        "other lines.\nChanges not saved are lost when another scene opens or the app ends.", m_SceneName.c_str());
    ImGui::Separator();

    for (const std::wstring& path : ListSceneFiles())
    {
        const std::string name = GetSceneName(path);
        if (ImGui::MenuItem(Utility::ToUTF8(Utility::RemoveExtension(Utility::RemoveBasePath(path))).c_str(), nullptr, name == m_SceneName))
        {
            std::string error;
            if (!OpenScene(path, error))
                Utility::Printf("Error: the scene file %s %s\n", name.c_str(), error.c_str());
        }
        ImGui::SetItemTooltip("%s", name.c_str());
    }
}

void ModelViewer::RegisterSceneCommand(void)
{
    ControlChannel::Register("scene", "scene open <scene file> | scene save: open opens the scene file, relative to the ModelViewer folder "
        "as -scene takes it, as the Scene menu does. The model changes, the objects take their settings in the scene file, so the camera "
        "takes the pose and vertical field of view of the scene file and the lights change, and the path tracer starts over. Changes to the objects not saved, "
        "such as a camera moved since, are lost. The settings, camera move speed, window and UI stay. "
        "Fails and changes nothing if the scene file cannot be opened. "
        "save writes the settings of the objects to the scene file open, as Save in the Scene menu does, and leaves its other lines. "
        "The camera is one of the objects, so save writes its current pose as Position, Heading and Pitch and its vertical field of view "
        "as Vertical FOV, as editor get Camera gives them, and drops a [Camera] Fit=Model. state scene has the scene file open", [this](const std::string& arguments, const ControlChannel::Reply& reply)
    {
        if (arguments == "save")
        {
            std::string error;
            if (!SaveScene(error))
            {
                reply.Error("the scene file " + m_SceneName + " " + error + ".");
                return;
            }
            reply.Ok(ControlChannel::Fields().Text("path", m_SceneName));
            return;
        }
        const std::string usage = "usage: scene open <scene file> | scene save";
        if (arguments.compare(0, 5, "open ") != 0 || arguments.size() == 5)
        {
            reply.Error(usage);
            return;
        }
        const std::string path = arguments.substr(5);
        std::string error;
        if (!OpenScene(Utility::UTF8ToWideString(path), error))
        {
            reply.Error("the scene file " + path + " " + error + ". The scene did not change.");
            return;
        }
        ControlChannel::Fields state;
        for (const char* group : { "scene", "model", "camera", "light", "sky" })
            state.Merge(ControlChannel::GetState(group));
        reply.Ok(ControlChannel::Fields().Object("state", state));
    });
}

// state의 renderer. scene 을 오가도 scene texture descriptor heap의 사용량이 늘지 않는지 harness가 본다 (ModelViewer::m_Models).
ControlChannel::Fields DescribeRenderer(void)
{
    return ControlChannel::Fields().Object("scene_texture_descriptors", ControlChannel::Fields()
        .Integer("allocated", Renderer::s_TextureHeap.GetAllocatedCount())
        .Integer("capacity", Renderer::s_TextureHeap.GetCapacity()));
}

void ModelViewer::Startup( void )
{
    MotionBlur::Enable = true;
    TemporalEffects::EnableTAA = true;
    FXAA::Enable = false;
    PostEffects::EnableHDR = true;
    PostEffects::EnableAdaptation = true;

    Renderer::Initialize();
    RayQuerySmokeTest::Run();
    RaytracingView::Initialize();
    RayPick::Initialize();
    PathTracerTuning::Initialize();
    DynamicGI::Initialize();
    BrdfTest::Initialize();

    uint32_t rebuildValue;
    if (CommandLineArgs::GetInteger(L"rebuild", rebuildValue))
        m_ForceRebuild = rebuildValue != 0;

    std::wstring scenePath = kDefaultScene;
    CommandLineArgs::GetString(L"scene", scenePath);
#ifdef LEGACY_RENDERER
    Sponza::Startup(m_Camera);
#endif
    m_Camera.SetZRange(1.0f, 10000.0f);
    m_CameraController.reset(new EditorCameraController(m_Camera, Vector3(kYUnitVector)));
    CameraComponent& camera = m_CameraObject.AddComponent<CameraComponent>(*m_CameraController, m_Camera, m_ModelInst);
    // scene 을 열 때 object가 scene file의 설정을 받도록 먼저 등록한다.
    Scene::AddObject(m_ModelObject);
    Scene::AddObject(m_CameraObject);
    Scene::AddObject(m_LightObject);
    Scene::AddObject(m_SkyObject);
    AssembleDynamicGIVolume(m_GIVolumeObject, [this] { return m_ModelInst.GetAxisAlignedBox(); });
    Scene::RegisterOptionalObject(m_GIVolumeObject);
#ifdef LEGACY_RENDERER
    // LEGACY_RENDERER는 scene file을 읽지 않으므로 Sponza::Startup이 정한 camera를 그대로 둔다.
    m_SceneName = GetSceneName(scenePath);
#else
    std::string error;
    if (!OpenScene(scenePath, error))
    {
        Utility::Printf("Error: the scene file %ws %s\n", scenePath.c_str(), error.c_str());
        m_CameraController->SetPose(CameraComponent::GetDefaultPose());
        m_SceneName = GetSceneName(scenePath);
    }
#endif
    EditorCameraController& cameraController = *m_CameraController;
    RegisterPoseCommand(cameraController);
    ControlChannel::RegisterState("camera", [this] { return DescribeCamera(m_Camera, *m_CameraController); });
    ControlChannel::RegisterState("scene", [this] { return DescribeScene(m_SceneName); });
    ControlChannel::RegisterState("model", [this] { return DescribeModel(m_ModelComponent.GetPath(), m_ModelInst); });
    ControlChannel::RegisterState("renderer", DescribeRenderer);
    RegisterSceneCommand();

    SceneEditor::RegisterComponentDetails<ModelComponent>(ShowModelDetails);
    SceneEditor::RegisterComponentDetails<CameraComponent>(ShowCameraDetails);
    SceneEditor::RegisterComponentDetails<LightComponent>(ShowLightDetails);
    SceneEditor::RegisterComponentDetails<SkyComponent>(ShowSkyDetails);
    SceneEditor::RegisterCommands(m_Camera);
    RegisterLight(m_Light);
    RegisterSky(m_Sky);
    RegisterSceneConditions();
    RegisterMeasurementConditions(camera, m_Light, m_Sky);
}

void ModelViewer::Cleanup( void )
{
    UnregisterMeasurementConditions();
    Scene::RemoveObject(m_GIVolumeObject);
    Scene::UnregisterOptionalObject(m_GIVolumeObject);
    Scene::RemoveObject(m_SkyObject);
    Scene::RemoveObject(m_LightObject);
    Scene::RemoveObject(m_CameraObject);
    Scene::RemoveObject(m_ModelObject);
    DynamicGI::Shutdown();
    PathTracerTuning::Shutdown();
    RayPick::Shutdown();
    m_RaytracingScene.Destroy();
    m_ModelInst = nullptr;
    m_Models.clear();

#ifdef LEGACY_RENDERER
    Sponza::Cleanup();
#endif

    Renderer::Shutdown();
}

namespace Graphics
{
    extern EnumVar DebugZoom;
}

void ModelViewer::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    ImGuiLayer::ShowMenu("Scene", [this] { ShowSceneMenu(); });

    if (GameInput::IsFirstPressed(GameInput::kLShoulder))
        DebugZoom.Decrement();
    else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
        DebugZoom.Increment();

    // scene buffer 크기는 scene viewport를 따라 frame마다 바뀔 수 있다.
    m_Camera.SetAspectRatio((float)g_SceneColorBuffer.GetHeight() / g_SceneColorBuffer.GetWidth());
    m_CameraController->Update(deltaT);

    SceneEditor::Update(m_Camera);
    DynamicGI::UpdateEditor(m_Camera);
    PathTracerTuning::Update();

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");

    m_ModelInst.Update(gfxContext, deltaT);
    m_RaytracingScene.AddModelInstance(m_ModelInst);
    m_RaytracingScene.Build(gfxContext);

    gfxContext.Finish();

    // We use viewport offsets to jitter sample positions from frame to frame (for TAA.)
    // D3D has a design quirk with fractional offsets such that the implicit scissor
    // region of a viewport is floor(TopLeftXY) and floor(TopLeftXY + WidthHeight), so
    // having a negative fractional top left, e.g. (-0.25, -0.25) would also shift the
    // BottomRight corner up by a whole integer.  One solution is to pad your viewport
    // dimensions with an extra pixel.  My solution is to only use positive fractional offsets,
    // but that means that the average sample position is +0.5, which I use when I disable
    // temporal AA.
    TemporalEffects::GetJitterOffset(m_MainViewport.TopLeftX, m_MainViewport.TopLeftY);

    m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    m_MainViewport.MinDepth = 0.0f;
    m_MainViewport.MaxDepth = 1.0f;

    m_MainScissor.left = 0;
    m_MainScissor.top = 0;
    m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();
}

void ModelViewer::RenderScene( void )
{
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();
    const D3D12_VIEWPORT& viewport = m_MainViewport;
    const D3D12_RECT& scissor = m_MainScissor;

    ParticleEffectManager::Update(gfxContext.GetComputeContext(), Graphics::GetFrameTime());

    if (m_ModelInst.IsNull())
    {
#ifdef LEGACY_RENDERER
        Sponza::RenderScene(gfxContext, m_Camera, viewport, scissor);
#endif
    }
    else
    {
        // Update global constants
        Vector3 LightDirection = m_Light.GetDirection();
        // directional light가 어느 방향에 있어도 shadow map이 model 전체를 덮게 light 방향에서 본 model의 AABB를 감싼다.
        m_ShadowCamera.UpdateMatrixToEnclose(-LightDirection, m_ModelInst.GetAxisAlignedBox(),
            (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

        GlobalConstants globals;
        globals.ViewProjMatrix = m_Camera.GetViewProjMatrix();
        globals.ShadowMatrix = m_ShadowCamera.GetShadowMatrix();
        globals.CameraPos = m_Camera.GetPosition();
        globals.LightDirection = LightDirection;
        globals.LightIlluminance = m_Light.GetIlluminance();

        // Begin rendering depth
        gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
        gfxContext.ClearDepth(g_SceneDepthBuffer);
        for (ColorBuffer& target : g_GBuffer)
        {
            gfxContext.TransitionResource(target, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
            gfxContext.ClearColor(target);
        }

        MeshSorter sorter(MeshSorter::kDefault);
		sorter.SetCamera(m_Camera);
		sorter.SetViewport(viewport);
		sorter.SetScissor(scissor);
		sorter.SetDepthStencilTarget(g_SceneDepthBuffer);
        for (ColorBuffer& target : g_GBuffer)
            sorter.AddRenderTarget(target);

        m_ModelInst.Render(sorter);

        sorter.Sort();

        {
            ScopedTimer _prof(L"G-Buffer Pass", gfxContext);
            sorter.RenderMeshes(MeshSorter::kOpaque, gfxContext, globals);
        }

        LinearDepth::Update(gfxContext.GetComputeContext(), m_Camera.GetNearClip(), m_Camera.GetFarClip());

        {
            ScopedTimer _outerprof(L"Main Render", gfxContext);

            {
                ScopedTimer _prof(L"Shadow Map", gfxContext);

                MeshSorter shadowSorter(MeshSorter::kShadows);
				shadowSorter.SetCamera(m_ShadowCamera);
				shadowSorter.SetDepthStencilTarget(g_ShadowBuffer);

                m_ModelInst.Render(shadowSorter);

                shadowSorter.Sort();
                shadowSorter.RenderMeshes(MeshSorter::kOpaque, gfxContext, globals);
            }

            const GIFrameInputs giInputs = {
                .camera = m_Camera,
                .rasterViewport = viewport,
                .scene = m_RaytracingScene,
                .lightDirection = LightDirection,
                .lightIlluminance = m_Light.GetIlluminance(),
                .skyRadiance = m_Sky.GetRadiance(),
                .shadowMap = g_ShadowBuffer,
                .shadowMatrix = m_ShadowCamera.GetShadowMatrix(),
            };
            DynamicGI::Update(gfxContext.GetComputeContext(), giInputs, g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());

            gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
            gfxContext.ClearColor(g_SceneColorBuffer);

            Renderer::ShadeOpaqueSurfaces(gfxContext.GetComputeContext(), m_Camera, globals, g_SceneColorBuffer);

            Renderer::DrawSkyBackground(gfxContext, viewport, scissor, m_Sky.GetRadiance());

            DynamicGI::DrawProbes(gfxContext, m_Camera, viewport, scissor);

            sorter.RenderMeshes(MeshSorter::kTransparent, gfxContext, globals);
        }
    }

    // Some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.  Everything
    // is static in our scene, so we generate velocity from camera motion and the depth buffer.  A velocity buffer
    // is necessary for all temporal effects (and motion blur).
    MotionBlur::GenerateCameraVelocityBuffer(gfxContext, m_Camera, true);

    TemporalEffects::ResolveImage(gfxContext);

    ParticleEffectManager::Render(gfxContext, m_Camera, g_SceneColorBuffer, g_SceneDepthBuffer,  g_LinearDepth[FrameIndex]);

    // Until I work out how to couple these two, it's "either-or".
    if (DepthOfField::Enable)
        DepthOfField::Render(gfxContext, m_Camera.GetNearClip(), m_Camera.GetFarClip());
    else
        MotionBlur::RenderObjectBlur(gfxContext, g_VelocityBuffer);

    // scene color의 마지막 pass 뒤에 덮어써서, PT가 raster와 같은 bloom, exposure, tone mapping을 거쳐 두 화면을 같은 조건으로 견주게 한다.
    // raster는 그대로 그린다. PT의 Shading DDGI Probe Hit는 이 frame의 shadow
    // pass가 그린 shadow map을 읽는다.
    PathTracerTuning::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, m_RaytracingScene, m_Light.GetDirection(), m_Light.GetIlluminance(),
        m_Sky.GetRadiance(), g_ShadowBuffer, m_ShadowCamera.GetShadowMatrix(), g_SceneColorBuffer);

    gfxContext.Finish();

    DynamicGI::MeasureProbes(m_RaytracingScene);
    RayPick::Render(m_Camera, m_MainViewport, g_SceneDepthBuffer, m_RaytracingScene);
}

void ModelViewer::RenderSceneOverlay( GraphicsContext& gfxContext )
{
    RaytracingView::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, m_RaytracingScene, m_Light.GetDirection(), g_SceneOverlayBuffer);
    DynamicGI::DrawOverlay(gfxContext, m_Camera);
}
