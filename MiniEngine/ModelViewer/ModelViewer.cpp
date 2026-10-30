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
#include "Model/Raytracing/RayQueryValidation.h"
#include "Model/Raytracing/RayPick.h"
#include "Model/PathTracer/PathTracer.h"
#include "Model/PathTracer/PathTracerTuning.h"
#include "Model/PathTracer/BrdfTest.h"
#include "Model/Loader/glTF.h"
#include "Model/Renderer/Renderer.h"
#include "Model/Model.h"
#include "Model/Loader/ModelLoader.h"
#include "Core/Camera/ShadowCamera.h"
#include "Core/Display/Display.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Editor/EditorCamera.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/App/UserSettings.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/UI/ImGuiLayer.h"
#include "Core/Utility/IniFile.h"

#include <algorithm>
#include <map>
#include <sstream>

// Dynamic GI 프로젝트에선 legacy path가 아닌 glTF path를 사용한다.
// legacy는 Blinn-Phong, glTF는 PBR shading
// Skinning/animation은 glTF path에만 있음
// #define LEGACY_RENDERER

using namespace GameCore;
using namespace Math;
using namespace Graphics;
using namespace std;

using Renderer::MeshSorter;

// KHR_lights_punctual의 directional light (references/glTF/extensions/2.0/Khronos/KHR_lights_punctual/README.md:110).
// 무한히 멀어서 감쇠하지 않고, 자기 transform의 local -Z 방향으로 빛을 낸다. transform의 위치는 조명에 영향을 주지 않고,
// scene editor에서 light를 보고 고르는 자리다.
class DirectionalLight : public SceneEditor::IconObject
{
public:
    // gizmo, light set 명령, Details panel, user.ini가 바꾸는 값
    struct Properties
    {
        // GetX, GetY, GetZ가 local의 +X, +Y, +Z를 world에서 본 방향이다. 조명이 쓰는 +Z를 계산 없이 그대로 읽도록
        // quaternion 대신 직교 matrix로 둔다.
        Matrix3 rotation;
        Vector3 position;
        // spec대로 color는 linear RGB이고 성분마다 0 ~ 1이며, intensity는 lux이고 0 이상이다.
        Vector3 color;
        float intensity;
    };

    DirectionalLight();

    const Properties& GetProperties() const { return m_Properties; }
    void SetProperties(const Properties& properties) { m_Properties = properties; }

    // surface에서 light를 향하는 방향. transform의 +Z다.
    Vector3 GetDirection() const { return m_Properties.rotation.GetZ(); }
    // light에 수직인 면의 illuminance (lux). spec은 color를 흰색 light의 intensity에 곱하는 값으로 정한다 (README.md:96).
    Vector3 GetIlluminance() const { return m_Properties.color * Scalar(m_Properties.intensity); }

    // 회전이 없는 transform으로 둔다. 빛은 world의 -Z로 가고 icon은 원점에 놓인다. color와 intensity는 그대로다.
    void ResetTransform();

    virtual const char* GetName() const override { return "Directional Light"; }
    // color와 intensity를 바꾸고 transform을 되돌린다.
    virtual void ShowDetails() override;
    virtual Vector3 GetPosition() const override { return m_Properties.position; }
    virtual void SetPosition(Vector3 position) override { m_Properties.position = position; }
    virtual void Draw(const Camera& camera, bool selected) const override;
    virtual void Rotate(const Camera& camera, ImGuizmo::MODE axes) override;

private:
    Properties m_Properties;
};

// ray가 아무 면도 만나지 않을 때 받는 빛. 무한히 먼 sky가 모든 방향에서 같은 radiance를 낸다.
// ray tracing으로 간접광을 구하는 곳 (path tracer, 앞으로의 DDGI)이 miss에서 이 radiance를 쓴다. raster는 이 빛으로 surface를 비추지
// 않고 camera에서 바로 보이는 배경으로만 그린다. raster에는 sky의 visibility가 없어서 surface에 더하면 sky가 가려진 실내까지 밝아지고,
// 그 빛은 DDGI가 구한다.
class SkyLight : public SceneEditor::Object
{
public:
    // Details panel, user.ini, settings 명령이 바꾸는 값
    struct Properties
    {
        // directional light처럼 color는 linear RGB이고 성분마다 0 ~ 1이며, intensity와 곱한 것이 빛이다. intensity는 lux가 아니라
        // radiance (cd/m²)이고 0 이상이다. ray가 받는 값이 radiance이고 glTF의 emissive도 cd/m²다.
        Vector3 color;
        float intensity;
    };

    SkyLight();

    const Properties& GetProperties() const { return m_Properties; }
    void SetProperties(const Properties& properties) { m_Properties = properties; }

    // 모든 방향에서 같은 radiance (cd/m²)
    Vector3 GetRadiance() const { return m_Properties.color * Scalar(m_Properties.intensity); }

    // 무한히 멀어 icon을 둘 자리가 없으므로 Scene Hierarchy로만 고른다.
    virtual const char* GetName() const override { return "Sky Light"; }
    virtual void ShowDetails() override;

private:
    Properties m_Properties;
};

// scene에 놓인 glTF model. 스스로 보이므로 icon이 없다. 아직 바꿀 수 있는 설정이 없어 Details panel에 model의 정보만 보인다.
class SceneModel : public SceneEditor::Object
{
public:
    explicit SceneModel(const ModelInstance& instance) : m_Instance(instance) {}

    // 이름은 file 이름에서 확장자를 뺀 것이다.
    void SetPath(const std::string& path);
    const std::string& GetPath() const { return m_Path; }

    virtual const char* GetName() const override { return m_Name.c_str(); }
    virtual void ShowDetails() override;

private:
    const ModelInstance& m_Instance;
    std::string m_Path;
    std::string m_Name;
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
    // PrepareScene이 준비한 scene 으로 바꾼다. moveCamera면 camera를 scene file의 처음 pose로 옮긴다. path tracer는 처음부터 다시 쌓는다.
    void ShowScene(const PreparedScene& scene, bool moveCamera);
    void ShowSceneMenu(void);
    void RegisterSceneCommand(void);
    // 측정 조건의 줄에 지금 scene 을 담고, 다른 scene 의 줄을 Restore하면 그 scene 을 연다.
    void RegisterSceneConditions(void);
    void ReadSceneConditions(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading);

    // 한 번 연 model을 glTF 경로마다 남겨 다시 쓴다. Renderer::LoadModel은 load할 때마다 scene texture descriptor heap에서
    // descriptor를 할당하고 풀지 않으므로, scene 을 오갈 때마다 load하면 heap이 찬다.
    std::map<std::wstring, std::shared_ptr<const Model>> m_Models;

    Camera m_Camera;
    unique_ptr<EditorCamera> m_CameraController;
    // -rebuild 1이면 처음 load하는 model의 .mini를 다시 만든다.
    bool m_ForceRebuild = false;
    // 연 scene. state와 측정 조건의 줄이 scene 을 가리키는 이름이다.
    std::string m_SceneName;

    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT m_MainScissor;

    ModelInstance m_ModelInst;
    SceneModel m_SceneModel{ m_ModelInst };
    RaytracingScene m_RaytracingScene;
    ShadowCamera m_ShadowCamera;
    DirectionalLight m_Light;
    SkyLight m_SkyLight;
};

CREATE_APPLICATION( ModelViewer )

const char kLightSettingsSection[] = "Directional Light";

// 예전에 방위 -0.5 rad와 고도 0.75 x 90°로 정하던 기본 방향을 bit 단위로 옮겨서, 기본 화면이 그때와 같다.
const Vector3 kDefaultLightDirection(0.335837096f, 0.923879147f, -0.18346864f);
// spec의 기본 intensity는 1 lux이지만, 기본 화면과 harness baseline의 Exposure가 맞춰 둔 밝기를 지키도록 4π로 둔다. 그 Exposure를
// 맞출 때의 raster는 diffuse에 1/π가 없어 intensity 4로 지금의 4π만큼 밝았다.
constexpr float kDefaultLightIntensity = 4.0f * XM_PI;

const char kSkyLightSettingsSection[] = "Sky Light";

// scripts/viewer.py가 이 이름과 형식으로 -scene이 없을 때 여는 scene file의 경로를 읽는다.
const wchar_t kDefaultScene[] = L"Scenes/Sponza.ini";
// Scene menu가 scene file을 찾는 폴더
const wchar_t kScenesFolder[] = L"Scenes/";

// 기본값은 맑은 날 한낮의 두 illuminance의 비로 정한다. 하나는 해가 비추는 면이고, 다른 하나는 해만 가려진 그늘이다.
// 그늘은 하늘의 빛만 받는다고 본다. 실제로는 그늘을 만든 물체가 하늘 일부를 가리고 빛을 반사하기도 하는데, 이 둘은 무시한다.
// Wikipedia "Daylight"의 illuminance 표는 앞의 것을 111,000 lux로, 뒤의 것을 20,000 lux로 든다. 표에는 출처 표기가 없고 값도
// 어림이다. sky의 radiance가 L이면 위를 향한 면은 πL lux를 받는다. 그래서 πL : kDefaultLightIntensity = 20,000 : 111,000이다.
constexpr float kDefaultSkyLightIntensity = kDefaultLightIntensity / XM_PI * (20000.0f / 111000.0f);

// direction을 그대로 +Z로 두고 +Y를 world의 위쪽에 가깝게 둔 rotation. direction은 단위 vector다.
Matrix3 MakeRotationToward(Vector3 direction)
{
    // 수직인 방향은 world의 위쪽과 함께 +X를 정하지 못하므로 world의 +Z를 위쪽 대신 쓴다.
    const Vector3 up = fabsf(direction.GetY()) > 0.9999f ? Vector3(kZUnitVector) : Vector3(kYUnitVector);
    const Vector3 x = Normalize(Cross(up, direction));
    return Matrix3(x, Cross(direction, x), direction);
}

// +Z와 +Y로 rotation을 만든다. 둘이 서로 직교하는 단위 vector가 아니면 false를 돌려준다.
// 두 vector를 그대로 쓰므로, 저장한 rotation의 두 vector를 다시 읽으면 bit 단위로 같은 rotation이 된다.
bool MakeRotation(Vector3 direction, Vector3 up, Matrix3& rotation)
{
    const float tolerance = 1e-4f;
    if (fabsf(Length(direction) - 1.0f) > tolerance || fabsf(Length(up) - 1.0f) > tolerance || fabsf(Dot(direction, up)) > tolerance)
        return false;
    rotation = Matrix3(Cross(up, direction), up, direction);
    return true;
}

DirectionalLight::DirectionalLight()
{
    m_Properties.rotation = MakeRotationToward(kDefaultLightDirection);
    m_Properties.position = Vector3(kZero);
    m_Properties.color = Vector3(1.0f, 1.0f, 1.0f);
    m_Properties.intensity = kDefaultLightIntensity;
}

void DirectionalLight::ResetTransform()
{
    m_Properties.rotation = Matrix3(kIdentity);
    m_Properties.position = Vector3(kZero);
}

bool IsColor(Vector3 color)
{
    const Vector3 clamped = Min(Max(color, Vector3(kZero)), Vector3(1.0f, 1.0f, 1.0f));
    return XMVector3Equal(color, clamped);
}

// count개의 숫자를 ','로 이은 text. 끝에 다른 글자가 있거나 유한하지 않은 숫자는 받지 않는다.
bool ReadNumbers(const std::string& text, float* numbers, int count)
{
    std::istringstream stream(text);
    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && stream.get() != ',')
            return false;
        if (!(stream >> numbers[i]) || !std::isfinite(numbers[i]))
            return false;
    }
    return stream.peek() == std::char_traits<char>::eof();
}

bool ReadVector(const std::string& text, Vector3& vector)
{
    float numbers[3];
    if (!ReadNumbers(text, numbers, 3))
        return false;
    vector = Vector3(numbers[0], numbers[1], numbers[2]);
    return true;
}

// 다시 읽으면 같은 float가 되는 자릿수로 쓴다.
std::string FormatVector(Vector3 vector)
{
    char text[96];
    sprintf_s(text, "%.9g,%.9g,%.9g", (float)vector.GetX(), (float)vector.GetY(), (float)vector.GetZ());
    return text;
}

void SaveLightSettings(const DirectionalLight& light, UserSettings::Values& values)
{
    const DirectionalLight::Properties& properties = light.GetProperties();
    values["Direction"] = FormatVector(properties.rotation.GetZ());
    values["Up"] = FormatVector(properties.rotation.GetY());
    values["Position"] = FormatVector(properties.position);
    values["Color"] = FormatVector(properties.color);
    char text[32];
    sprintf_s(text, "%.9g", properties.intensity);
    values["Intensity"] = text;
}

// 받을 수 없는 값은 건너뛰고 그 값은 그대로 둔다.
void LoadLightSettings(DirectionalLight& light, const UserSettings::Values& values)
{
    const auto read = [&values](const char* key, Vector3& vector)
    {
        const auto text = values.find(key);
        return text != values.end() && ReadVector(text->second, vector);
    };

    DirectionalLight::Properties properties = light.GetProperties();
    Vector3 direction, up, position, color;
    if (read("Direction", direction) && read("Up", up))
        MakeRotation(direction, up, properties.rotation);
    if (read("Position", position))
        properties.position = position;
    if (read("Color", color) && IsColor(color))
        properties.color = color;
    const auto intensity = values.find("Intensity");
    float number = 0.0f;
    if (intensity != values.end() && ReadNumbers(intensity->second, &number, 1) && number >= 0.0f)
        properties.intensity = number;
    light.SetProperties(properties);
}

SkyLight::SkyLight()
{
    m_Properties.color = Vector3(1.0f, 1.0f, 1.0f);
    m_Properties.intensity = kDefaultSkyLightIntensity;
}

void SaveSkyLightSettings(const SkyLight& sky, UserSettings::Values& values)
{
    const SkyLight::Properties& properties = sky.GetProperties();
    values["Color"] = FormatVector(properties.color);
    char text[32];
    sprintf_s(text, "%.9g", properties.intensity);
    values["Intensity"] = text;
}

// 받을 수 없는 값은 건너뛰고 그 값은 그대로 둔다. directional light와 달리 intensity의 inf는 받는다.
// scripts/check-path-tracer-export.py가 inf로 두어 path tracer에 NaN과 Inf인 sample을 만든다.
void LoadSkyLightSettings(SkyLight& sky, const UserSettings::Values& values)
{
    SkyLight::Properties properties = sky.GetProperties();
    const auto color = values.find("Color");
    Vector3 readColor;
    if (color != values.end() && ReadVector(color->second, readColor) && IsColor(readColor))
        properties.color = readColor;
    const auto intensity = values.find("Intensity");
    float number = 0.0f;
    if (intensity != values.end() && sscanf_s(intensity->second.c_str(), "%f", &number) == 1 && !std::isnan(number) && number >= 0.0f)
        properties.intensity = number;
    sky.SetProperties(properties);
}

// state의 sky
ControlChannel::Fields DescribeSkyLight(const SkyLight& sky)
{
    const SkyLight::Properties& properties = sky.GetProperties();
    return ControlChannel::Fields()
        .Object("color", ControlChannel::Fields()
            .Float("r", properties.color.GetX()).Float("g", properties.color.GetY()).Float("b", properties.color.GetZ()))
        .Float("intensity", properties.intensity);
}

// sky light를 user.ini의 section과 harness의 settings 명령, state로 읽고 바꾸게 한다.
void RegisterSkyLight(SkyLight& sky)
{
    UserSettings::Register(kSkyLightSettingsSection,
        [&sky](UserSettings::Values& values) { SaveSkyLightSettings(sky, values); },
        [&sky](const UserSettings::Values& values) { LoadSkyLightSettings(sky, values); });
    ControlChannel::RegisterState("sky", [&sky] { return DescribeSkyLight(sky); });
}

void SkyLight::ShowDetails()
{
    float color[3] = { m_Properties.color.GetX(), m_Properties.color.GetY(), m_Properties.color.GetZ() };
    if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_Float))
        m_Properties.color = Min(Max(Vector3(color[0], color[1], color[2]), Vector3(kZero)), Vector3(1.0f, 1.0f, 1.0f));
    ImGui::SetItemTooltip("Linear RGB, each 0 to 1.");
    if (ImGui::DragFloat("Intensity", &m_Properties.intensity, 0.01f, 0.0f, FLT_MAX, "%.3f cd/m2"))
        m_Properties.intensity = std::max(0.0f, m_Properties.intensity);
    ImGui::SetItemTooltip("Radiance of the sky in every direction, in cd/m2. An upward surface open to the whole sky receives "
        "pi times this in lux.");
    ImGui::TextDisabled("Rays that hit nothing see this light. The raster does not light surfaces with it and draws it only as the background.");
}

void SceneModel::SetPath(const std::string& path)
{
    m_Path = path;
    m_Name = Utility::RemoveExtension(Utility::RemoveBasePath(path));
}

void SceneModel::ShowDetails()
{
    const Model& model = m_Instance.GetModel();
    const AxisAlignedBox bounds = m_Instance.GetAxisAlignedBox();
    const Vector3 min = bounds.GetMin();
    const Vector3 max = bounds.GetMax();
    ImGui::TextWrapped("glTF: %s", m_Path.c_str());
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

void DescribeCameraConditions(const EditorCamera& camera, MeasurementConditions::Values& values)
{
    const EditorCamera::Pose pose = camera.GetPose();
    values["camera"] = FormatVector(pose.position);
    values["heading"] = FormatFloat(pose.heading);
    values["pitch"] = FormatFloat(pose.pitch);
}

void ReadCameraConditions(EditorCamera& camera, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    EditorCamera::Pose pose = camera.GetPose();
    ReadValue(values, "camera", reading, [&pose](const std::string& text) { return ReadVector(text, pose.position); });
    ReadValue(values, "heading", reading, [&pose](const std::string& text) { return ReadNumbers(text, &pose.heading, 1); });
    ReadValue(values, "pitch", reading, [&pose](const std::string& text) { return ReadNumbers(text, &pose.pitch, 1); });
    reading.applies.push_back([&camera, pose] { camera.SetPose(pose); });
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

// light의 rotation은 user.ini처럼 +Z와 +Y로 적어, 다시 읽으면 bit 단위로 같은 rotation이 된다. position은 조명에 영향이 없어 적지 않는다.
void DescribeLightConditions(const DirectionalLight& light, MeasurementConditions::Values& values)
{
    const DirectionalLight::Properties& properties = light.GetProperties();
    values["light direction"] = FormatVector(properties.rotation.GetZ());
    values["light up"] = FormatVector(properties.rotation.GetY());
    values["light color"] = FormatVector(properties.color);
    values["light intensity"] = FormatFloat(properties.intensity);
}

void ReadLightConditions(DirectionalLight& light, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    DirectionalLight::Properties properties = light.GetProperties();
    Vector3 direction, up;
    const bool hasDirection = ReadValue(values, "light direction", reading, [&direction](const std::string& text) { return ReadVector(text, direction); });
    const bool hasUp = ReadValue(values, "light up", reading, [&up](const std::string& text) { return ReadVector(text, up); });
    if (hasDirection && hasUp && !MakeRotation(direction, up, properties.rotation))
    {
        reading.unread.push_back("light direction");
        reading.unread.push_back("light up");
    }
    ReadValue(values, "light color", reading, [&properties](const std::string& text) { return ReadVector(text, properties.color) && IsColor(properties.color); });
    ReadValue(values, "light intensity", reading, [&properties](const std::string& text)
    {
        return ReadNumbers(text, &properties.intensity, 1) && properties.intensity >= 0.0f;
    });
    reading.applies.push_back([&light, properties] { light.SetProperties(properties); });
}

void DescribeSkyLightConditions(const SkyLight& sky, MeasurementConditions::Values& values)
{
    const SkyLight::Properties& properties = sky.GetProperties();
    values["sky color"] = FormatVector(properties.color);
    values["sky intensity"] = FormatFloat(properties.intensity);
}

// intensity는 Sky Light의 settings처럼 inf도 받는다 (LoadSkyLightSettings). /fp:fast에서는 NaN과의 비교가 참일 수 있어 NaN을 따로 본다.
void ReadSkyLightConditions(SkyLight& sky, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    SkyLight::Properties properties = sky.GetProperties();
    ReadValue(values, "sky color", reading, [&properties](const std::string& text) { return ReadVector(text, properties.color) && IsColor(properties.color); });
    ReadValue(values, "sky intensity", reading, [&properties](const std::string& text)
    {
        return MeasurementConditions::ReadFloat(text, properties.intensity) && !std::isnan(properties.intensity) && properties.intensity >= 0.0f;
    });
    reading.applies.push_back([&sky, properties] { sky.SetProperties(properties); });
}

// raster와 path tracer가 그리는 조건을 Log와 capture의 줄에 담고 Restore로 되돌리게 한다. scene 은 ModelViewer::RegisterSceneConditions가,
// path tracer의 설정은 PathTracerTuning이 더한다.
void RegisterMeasurementConditions(EditorCamera& editorCamera, DirectionalLight& light, SkyLight& sky)
{
    MeasurementConditions::Register("camera", {
        [&editorCamera](MeasurementConditions::Values& values) { DescribeCameraConditions(editorCamera, values); },
        [&editorCamera](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadCameraConditions(editorCamera, values, reading); },
        { "camera" } });
    MeasurementConditions::Register("size", { DescribeSceneSize, ReadSceneSize, {} });
    MeasurementConditions::Register("taa", { DescribeTaa, ReadTaa, { "settings.Engine Tuning" } });
    MeasurementConditions::Register("light", {
        [&light](MeasurementConditions::Values& values) { DescribeLightConditions(light, values); },
        [&light](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadLightConditions(light, values, reading); },
        { "light" } });
    MeasurementConditions::Register("sky", {
        [&sky](MeasurementConditions::Values& values) { DescribeSkyLightConditions(sky, values); },
        [&sky](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadSkyLightConditions(sky, values, reading); },
        { "sky" } });
}

void UnregisterMeasurementConditions(void)
{
    for (const char* group : { "scene", "camera", "size", "taa", "light", "sky" })
        MeasurementConditions::Unregister(group);
}

ControlChannel::Fields DescribeVector(Vector3 vector)
{
    return ControlChannel::Fields().Float("x", vector.GetX()).Float("y", vector.GetY()).Float("z", vector.GetZ());
}

// state의 camera
ControlChannel::Fields DescribeCamera(const Camera& camera, const EditorCamera& editorCamera)
{
    const Vector3 position = camera.GetPosition();
    const EditorCamera::Pose pose = editorCamera.GetPose();
    return ControlChannel::Fields()
        .Text("controller", "editor")
        .Float("x", position.GetX())
        .Float("y", position.GetY())
        .Float("z", position.GetZ())
        .Float("heading", pose.heading)
        .Float("pitch", pose.pitch)
        .Float("move_speed", editorCamera.GetMoveSpeed())
        .Object("forward", DescribeVector(camera.GetForwardVec()))
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
        .Object("bounds", ControlChannel::Fields().Object("min", DescribeVector(bounds.GetMin())).Object("max", DescribeVector(bounds.GetMax())));
}

// harness가 camera를 숫자로 읽고 정한다.
void RegisterPoseCommand(EditorCamera& editorCamera)
{
    ControlChannel::SetStatus("camera", "editor");
    ControlChannel::Register("pose", "pose | pose <x> <y> <z> <heading> <pitch> | pose look-at <x> <y> <z>: the editor camera, "
        "in world units and radians as in state camera. look-at keeps the position and turns the camera toward the point. "
        "Looking straight up or down keeps the heading", [&editorCamera](const std::string& arguments, const ControlChannel::Reply& reply)
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
            if (LengthSquare(target - editorCamera.GetPose().position) == 0.0f)
            {
                reply.Error("the point is where the camera is, so there is no direction to look in");
                return;
            }
            editorCamera.LookAt(target);
        }
        else if (!arguments.empty())
        {
            if (sscanf_s(arguments.c_str(), "%f %f %f %f %f", &x, &y, &z, &heading, &pitch) != 5)
            {
                reply.Error(usage);
                return;
            }
            editorCamera.SetPose({ Vector3(x, y, z), heading, pitch });
        }

        const EditorCamera::Pose pose = editorCamera.GetPose();
        reply.Ok(ControlChannel::Fields()
            .Float("x", pose.position.GetX())
            .Float("y", pose.position.GetY())
            .Float("z", pose.position.GetZ())
            .Float("heading", pose.heading)
            .Float("pitch", pose.pitch)
            .Object("state", ControlChannel::GetState("camera")));
    });
}

float GetElevation(Vector3 direction)
{
    return XMConvertToDegrees(asinf(Clamp((float)direction.GetY(), -1.0f, 1.0f)));
}

// [0, 360)
float WrapDegrees(float degrees)
{
    const float wrapped = fmodf(degrees, 360.0f);
    if (wrapped >= 0.0f)
        return wrapped;
    // -0.00001처럼 360을 더하면 360으로 반올림되는 값은 0이다.
    return wrapped + 360.0f < 360.0f ? wrapped + 360.0f : 0.0f;
}

// 수직인 방향은 방위가 없으므로 0이다.
float GetAzimuth(Vector3 direction)
{
    const float x = direction.GetX();
    const float z = direction.GetZ();
    return x == 0.0f && z == 0.0f ? 0.0f : WrapDegrees(XMConvertToDegrees(atan2f(z, x)));
}

float GetDegreesBetween(float first, float second)
{
    const float difference = WrapDegrees(first - second);
    return std::min(difference, 360.0f - difference);
}

// 고도와 방위 (degree)가 가리키는 방향. 고도가 ±90이면 방위와 상관없이 수직이다.
Vector3 GetDirectionAt(float elevation, float azimuth)
{
    if (fabsf(elevation) == 90.0f)
        return Vector3(0.0f, elevation > 0.0f ? 1.0f : -1.0f, 0.0f);
    const float e = XMConvertToRadians(elevation);
    const float a = XMConvertToRadians(azimuth);
    return Normalize(Vector3(cosf(e) * cosf(a), sinf(e), cosf(e) * sinf(a)));
}

// state의 light. rotation은 glTF node의 rotation처럼 x, y, z, w 순서의 quaternion이고, w가 0 이상이 되는 쪽으로 쓴다.
// 고도와 방위는 조명이 쓰는 방향에서 계산한다.
ControlChannel::Fields DescribeLight(const DirectionalLight& light)
{
    const DirectionalLight::Properties& properties = light.GetProperties();
    Quaternion rotation((XMMATRIX)properties.rotation);
    if (XMVectorGetW(rotation) < 0.0f)
        rotation = -rotation;
    const Vector3 direction = light.GetDirection();
    return ControlChannel::Fields()
        .Object("rotation", ControlChannel::Fields()
            .Float("x", XMVectorGetX(rotation)).Float("y", XMVectorGetY(rotation)).Float("z", XMVectorGetZ(rotation)).Float("w", XMVectorGetW(rotation)))
        .Object("direction", DescribeVector(direction))
        .Float("elevation", GetElevation(direction))
        .Float("azimuth", GetAzimuth(direction))
        .Object("color", ControlChannel::Fields()
            .Float("r", properties.color.GetX()).Float("g", properties.color.GetY()).Float("b", properties.color.GetZ()))
        .Float("intensity", properties.intensity)
        .Object("position", DescribeVector(properties.position));
}

// 받은 값과 다시 읽은 고도와 방위가 같다고 보는 차이. degree와 radian을 오가며 생기는 float 오차보다 크게 잡았다.
constexpr float kLightDegreesTolerance = 1e-4f;

// gizmo와 Details panel이 바꾸는 값을 같은 방식으로 바꾼다.
// 받지 않는 값이 하나라도 있으면 아무것도 바꾸지 않도록, 값의 사본에 먼저 적용해 읽어 본다.
void SetLight(DirectionalLight& light, const std::string& arguments, const ControlChannel::Reply& reply)
{
    const std::string usage = "usage: light set rotation=<x>,<y>,<z>,<w> | direction=<x>,<y>,<z> | elevation=<degrees> azimuth=<degrees> | "
        "position=<x>,<y>,<z> | color=<r>,<g>,<b> | intensity=<lux>";
    const std::map<std::string, int> counts = { { "rotation", 4 }, { "direction", 3 }, { "position", 3 }, { "color", 3 },
        { "elevation", 1 }, { "azimuth", 1 }, { "intensity", 1 } };
    std::map<std::string, std::vector<float>> values;
    std::istringstream tokens(arguments);
    for (std::string token; tokens >> token;)
    {
        const size_t separator = token.find('=');
        const std::string key = token.substr(0, separator);
        const auto count = counts.find(key);
        if (separator == std::string::npos || count == counts.end() || values.count(key) != 0)
        {
            reply.Error("'" + token + "' is not a key=value of light set, or repeats a key. Nothing was set. " + usage);
            return;
        }
        float numbers[4];
        if (!ReadNumbers(token.substr(separator + 1), numbers, count->second))
        {
            reply.Error(key + " takes " + std::to_string(count->second) + " numbers joined by ','. Nothing was set. " + usage);
            return;
        }
        values[key].assign(numbers, numbers + count->second);
    }
    if (values.empty())
    {
        reply.Error(usage);
        return;
    }
    const bool hasElevation = values.count("elevation") != 0;
    const bool hasAzimuth = values.count("azimuth") != 0;
    if (values.count("rotation") + values.count("direction") + (hasElevation || hasAzimuth ? 1 : 0) > 1)
    {
        reply.Error("rotation, direction and elevation with azimuth each set the rotation, so only one of them is given. Nothing was set.");
        return;
    }

    DirectionalLight::Properties properties = light.GetProperties();
    ControlChannel::Fields rejected;
    bool anyRejected = false;
    if (values.count("rotation") != 0)
    {
        const std::vector<float>& xyzw = values["rotation"];
        const Quaternion requested(XMVectorSet(xyzw[0], xyzw[1], xyzw[2], xyzw[3]));
        if (XMVectorGetX(XMQuaternionLengthSq(requested)) == 0.0f)
        {
            reply.Error("rotation has no length. Nothing was set.");
            return;
        }
        properties.rotation = Matrix3(Normalize(requested));
    }
    if (values.count("direction") != 0)
    {
        const std::vector<float>& xyz = values["direction"];
        const Vector3 requested(xyz[0], xyz[1], xyz[2]);
        if (LengthSquare(requested) == 0.0f)
        {
            reply.Error("direction has no length. Nothing was set.");
            return;
        }
        properties.rotation = MakeRotationToward(Normalize(requested));
    }
    ControlChannel::Fields fields;
    if (hasElevation || hasAzimuth)
    {
        const Vector3 current = light.GetDirection();
        const float elevation = hasElevation ? values["elevation"][0] : GetElevation(current);
        const float azimuth = hasAzimuth ? values["azimuth"][0] : GetAzimuth(current);
        if (elevation < -90.0f || elevation > 90.0f)
        {
            rejected.Float("elevation", elevation);
            anyRejected = true;
        }
        else
        {
            const Vector3 direction = GetDirectionAt(elevation, azimuth);
            properties.rotation = MakeRotationToward(direction);
            if (hasElevation && fabsf(GetElevation(direction) - elevation) > kLightDegreesTolerance)
            {
                rejected.Float("elevation", GetElevation(direction));
                anyRejected = true;
            }
            if (fabsf(elevation) == 90.0f)
                fields.Text("note", "a vertical direction has no azimuth, so state light gives azimuth 0");
            else if (hasAzimuth && GetDegreesBetween(GetAzimuth(direction), azimuth) > kLightDegreesTolerance)
            {
                rejected.Float("azimuth", GetAzimuth(direction));
                anyRejected = true;
            }
        }
    }
    if (values.count("position") != 0)
        properties.position = Vector3(values["position"][0], values["position"][1], values["position"][2]);
    if (values.count("color") != 0)
    {
        const Vector3 color(values["color"][0], values["color"][1], values["color"][2]);
        if (IsColor(color))
            properties.color = color;
        else
        {
            rejected.Object("color", ControlChannel::Fields().Float("r", color.GetX()).Float("g", color.GetY()).Float("b", color.GetZ()));
            anyRejected = true;
        }
    }
    if (values.count("intensity") != 0)
    {
        if (values["intensity"][0] >= 0.0f)
            properties.intensity = values["intensity"][0];
        else
        {
            rejected.Float("intensity", values["intensity"][0]);
            anyRejected = true;
        }
    }
    if (anyRejected)
    {
        reply.Error("the light cannot take some values, so nothing was set. rejected has them, or what they would read back as. "
            "elevation is -90 to 90, each color component is 0 to 1, and intensity is 0 or more.",
            ControlChannel::Fields().Object("rejected", rejected));
        return;
    }

    light.SetProperties(properties);
    reply.Ok(fields.Object("state", ControlChannel::GetState("light")));
}

// light를 user.ini의 section, harness의 light 명령과 state로 읽고 바꾸게 한다.
void RegisterLight(DirectionalLight& light)
{
    UserSettings::Register(kLightSettingsSection,
        [&light](UserSettings::Values& values) { SaveLightSettings(light, values); },
        [&light](const UserSettings::Values& values) { LoadLightSettings(light, values); });
    ControlChannel::RegisterState("light", [&light] { return DescribeLight(light); });
    ControlChannel::Register("light", "light set <key>=<value> ... | light reset: the directional light, as its rotate and translate gizmos and its "
        "Details panel change it. reset sets the transform to no rotation at the origin, so the light goes along world -Z, "
        "and keeps the color and intensity, as Reset Transform in the Details panel does. It is a KHR_lights_punctual directional light and goes along the local -Z of its transform. "
        "rotation=<x>,<y>,<z>,<w> is the quaternion of the transform, as a glTF node's rotation. "
        "direction=<x>,<y>,<z> points from a surface toward the light, the local +Z, and is normalized. "
        "elevation=<degrees> is up from the horizon, -90 to 90. azimuth=<degrees> turns about +y from +x toward +z, 0 to 360. "
        "rotation, direction and elevation with azimuth each set the rotation, so only one of them is given. "
        "direction, elevation and azimuth keep the local +Y as near to world +y as they can. "
        "position=<x>,<y>,<z> in world units only places the icon, as the light is infinitely far. "
        "color=<r>,<g>,<b> is linear, each 0 to 1. intensity=<lux> is the illuminance on a surface facing the light, 0 or more. "
        "Nothing is set unless every value reads back as given. state light has these keys, with the direction the lighting uses",
        [&light](const std::string& arguments, const ControlChannel::Reply& reply)
    {
        if (arguments == "reset")
        {
            light.ResetTransform();
            reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("light")));
            return;
        }
        if (arguments.compare(0, 4, "set ") != 0)
        {
            reply.Error("usage: light set <key>=<value> ... | light reset. state light reads the light");
            return;
        }
        SetLight(light, arguments.substr(4), reply);
    });
}

void DrawArrow(ImDrawList& drawList, ImVec2 from, ImVec2 to, ImU32 color, float thickness)
{
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = sqrtf(dx * dx + dy * dy);
    const float headLength = thickness * 4.0f;
    if (length < headLength)
        return;

    const ImVec2 forward(dx / length, dy / length);
    const ImVec2 headBase(to.x - forward.x * headLength, to.y - forward.y * headLength);
    const ImVec2 side(-forward.y * headLength * 0.5f, forward.x * headLength * 0.5f);
    drawList.AddLine(from, headBase, color, thickness);
    drawList.AddTriangleFilled(to, ImVec2(headBase.x + side.x, headBase.y + side.y), ImVec2(headBase.x - side.x, headBase.y - side.y), color);
}

void DirectionalLight::Draw(const Camera& camera, bool selected) const
{
    ImVec2 icon;
    if (!SceneEditor::ProjectToScene(camera, m_Properties.position, icon))
        return;

    const ImU32 lightColor = IM_COL32(255, 200, 0, 255);
    const ImU32 selectedColor = IM_COL32(255, 128, 16, 255);
    const float radius = SceneEditor::GetIconRadius();
    const float thickness = radius * 0.15f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 화살표는 빛이 나아가는 방향, 곧 local -Z다.
    const float arrowLength = radius * 4.0f * SceneEditor::GetWorldLengthPerPixel(camera, m_Properties.position);
    ImVec2 arrowTip;
    if (SceneEditor::ProjectToScene(camera, m_Properties.position - GetDirection() * arrowLength, arrowTip))
        DrawArrow(*drawList, icon, arrowTip, lightColor, thickness);

    drawList->AddCircleFilled(icon, radius * 0.45f, lightColor);
    const int kRayCount = 8;
    for (int i = 0; i < kRayCount; ++i)
    {
        const float angle = XM_2PI * i / kRayCount;
        const ImVec2 direction(cosf(angle), sinf(angle));
        drawList->AddLine(
            ImVec2(icon.x + direction.x * radius * 0.65f, icon.y + direction.y * radius * 0.65f),
            ImVec2(icon.x + direction.x * radius, icon.y + direction.y * radius),
            lightColor, thickness);
    }

    if (selected)
        drawList->AddCircle(icon, radius * 1.2f, selectedColor, 0, thickness);
}

void DirectionalLight::Rotate(const Camera& camera, ImGuizmo::MODE axes)
{
    Matrix4 transform(m_Properties.rotation, m_Properties.position);
    if (!SceneEditor::Manipulate(camera, ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z, axes, transform))
        return;
    // gizmo가 돌린 matrix는 float 오차로 직교에서 조금씩 벗어나므로 quaternion을 거쳐 직교 matrix로 되돌린다.
    m_Properties.rotation = Matrix3(Normalize(Quaternion((XMMATRIX)transform.Get3x3())));
}

void DirectionalLight::ShowDetails()
{
    float color[3] = { m_Properties.color.GetX(), m_Properties.color.GetY(), m_Properties.color.GetZ() };
    if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_Float))
        m_Properties.color = Min(Max(Vector3(color[0], color[1], color[2]), Vector3(kZero)), Vector3(1.0f, 1.0f, 1.0f));
    ImGui::SetItemTooltip("Linear RGB, each 0 to 1.");
    if (ImGui::DragFloat("Intensity", &m_Properties.intensity, 0.05f, 0.0f, FLT_MAX, "%.3f lux"))
        m_Properties.intensity = std::max(0.0f, m_Properties.intensity);
    ImGui::SetItemTooltip("Illuminance on a surface facing the light, in lux.");

    ImGui::SeparatorText("Transform");
    const Vector3 direction = GetDirection();
    ImGui::Text("Toward the light: %.3f, %.3f, %.3f", (float)direction.GetX(), (float)direction.GetY(), (float)direction.GetZ());
    ImGui::Text("Elevation %.2f, azimuth %.2f degrees", GetElevation(direction), GetAzimuth(direction));
    if (ImGui::Button("Reset Transform"))
        ResetTransform();
    ImGui::SetItemTooltip("Sets the transform to no rotation at the origin, so the light goes along world -Z. The color and intensity stay.");
    ImGui::TextDisabled("Rotate the light with the gizmo (E). The toolbar on the scene picks local or world rings.");
}

// scene 은 scene file 하나로 연다. 형식은 IniFile이고 key는 아래와 같다. 실행 중에는 Scene menu와 scene open 명령으로 다른 scene 을 연다.
//   [Model]  glTF: scene file이 있는 폴더 기준의 glTF 경로
//            Scale: glTF 좌표에 곱해 world 좌표로 바꾸는 배율. 다음의 Radius와 둘 중 하나만 둔다.
//            Radius: model의 bounding sphere 반지름이 이 world 길이가 되는 배율 (ModelInstance::Resize)
//   [Camera] Position=<x>,<y>,<z>, Heading, Pitch: 처음 pose. 단위는 state camera와 pose 명령과 같다.
//            Fit=Model: 위 셋 대신 둔다. bounding sphere 중심의 +z 쪽 반지름의 2배와 near clip을 더한 거리에서 -z를 본다.
//            예전 -model의 OrbitCamera가 처음 그린 화면과 bit 단위로 같다.
// scene 을 열면 camera는 scene file의 pose로 시작한다. 다른 scene 의 줄을 Restore하면 줄의 camera를 쓴다. user.ini는 camera의 이동 속도만
// 남긴다.
struct SceneFile
{
    // scale과 radius 중 0이 아닌 하나만 쓴다.
    std::wstring gltfPath;
    float scale = 0.0f;
    float radius = 0.0f;
    bool fitModel = false;
    EditorCamera::Pose pose = { Vector3(kZero), 0.0f, 0.0f };
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

// 받을 수 없는 값이 있으면 error에 이유를 쓰고 false를 돌려준다.
bool ReadSceneFile(const std::wstring& path, SceneFile& scene, std::string& error)
{
    IniFile::Sections sections = IniFile::Read(path);
    const IniFile::Values& model = sections["Model"];
    const IniFile::Values& camera = sections["Camera"];
    const auto find = [](const IniFile::Values& values, const char* key) -> const std::string*
    {
        const auto value = values.find(key);
        return value == values.end() ? nullptr : &value->second;
    };

    const std::string* gltf = find(model, "glTF");
    if (gltf == nullptr)
    {
        error = "has no [Model] glTF, or cannot be read";
        return false;
    }
    scene.gltfPath = ResolveFromSceneFolder(path, Utility::UTF8ToWideString(*gltf));

    const std::string* scale = find(model, "Scale");
    const std::string* radius = find(model, "Radius");
    if ((scale == nullptr) == (radius == nullptr) || !ReadNumbers(scale != nullptr ? *scale : *radius, scale != nullptr ? &scene.scale : &scene.radius, 1) ||
        scene.scale < 0.0f || scene.radius < 0.0f || scene.scale + scene.radius == 0.0f)
    {
        error = "needs either [Model] Scale or Radius as a number above 0";
        return false;
    }

    const std::string* fit = find(camera, "Fit");
    const std::string* position = find(camera, "Position");
    const std::string* heading = find(camera, "Heading");
    const std::string* pitch = find(camera, "Pitch");
    scene.fitModel = fit != nullptr && *fit == "Model";
    const bool hasPose = position != nullptr && heading != nullptr && pitch != nullptr && ReadVector(*position, scene.pose.position) &&
        ReadNumbers(*heading, &scene.pose.heading, 1) && ReadNumbers(*pitch, &scene.pose.pitch, 1);
    if (scene.fitModel == hasPose || (fit != nullptr && !scene.fitModel))
    {
        error = "needs either [Camera] Fit=Model or Position=<x>,<y>,<z> with Heading and Pitch";
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

EditorCamera::Pose GetStartPose(const SceneFile& scene, const ModelInstance& model, float nearClip)
{
    if (!scene.fitModel)
        return scene.pose;
    const BoundingSphere sphere = model.GetBoundingSphere();
    return { sphere.GetCenter() + Vector3(0.0f, 0.0f, sphere.GetRadius() * 2.0f + nearClip), 0.0f, 0.0f };
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

void ModelViewer::ShowScene(const PreparedScene& scene, bool moveCamera)
{
    // 지난 frame의 command가 아직 지금 model instance의 buffer와 TLAS를 읽을 수 있으므로 GPU가 끝내기를 기다린 뒤에 바꾼다.
    g_CommandManager.IdleGPU();
    // TLAS의 buffer는 처음 Build한 scene 의 instance 수에 맞춰 만들므로 새 scene 에 맞게 다시 만들게 한다.
    m_RaytracingScene.Destroy();
    m_ModelInst = scene.model;
    ScaleModel(m_ModelInst, scene.file);
    if (moveCamera)
        m_CameraController->SetPose(GetStartPose(scene.file, m_ModelInst, m_Camera.GetNearClip()));
    m_SceneModel.SetPath(Utility::ToUTF8(scene.file.gltfPath));
    m_SceneName = scene.name;
    PathTracer::Reset();
}

bool ModelViewer::OpenScene(const std::wstring& path, std::string& error)
{
    PreparedScene scene;
    if (!PrepareScene(path, scene, error))
        return false;
    ShowScene(scene, true);
    return true;
}

// scene이 없는 줄은 scene file을 쓰기 전의 줄이고, 그때는 -model로 연 scene 을 Restore할 수 없었으므로 kDefaultScene의 줄로 읽는다.
// 줄의 scene 이 지금 scene 과 다르면 줄의 나머지를 받을 수 있을 때만 그 scene 을 준비해 보고, 열 수 없으면 아무것도 되돌리지 않는다.
// 연 scene 의 camera는 줄의 camera이므로 scene file의 처음 pose로 옮기지 않는다.
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
        reading.applies.push_back([this, prepared] { ShowScene(*prepared, false); });
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

// 지금 scene 에 표시를 둔다. 항목을 누르면 scene open 명령처럼 그 scene 을 연다.
void ModelViewer::ShowSceneMenu(void)
{
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
    ControlChannel::Register("scene", "scene open <scene file>: opens the scene file, relative to the ModelViewer folder as -scene takes it, "
        "as the Scene menu does. The model changes, the camera moves to the starting pose of the scene file and the path tracer starts over. "
        "The lights, settings, camera move speed, window and UI stay. Fails and changes nothing if the scene file cannot be opened. "
        "state scene has the scene file open", [this](const std::string& arguments, const ControlChannel::Reply& reply)
    {
        const std::string usage = "usage: scene open <scene file>";
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
        for (const char* group : { "scene", "model", "camera" })
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
    m_CameraController.reset(new EditorCamera(m_Camera, Vector3(kYUnitVector)));
#ifdef LEGACY_RENDERER
    // LEGACY_RENDERER는 scene file을 읽지 않으므로 Sponza::Startup이 정한 camera를 그대로 둔다.
    m_SceneName = GetSceneName(scenePath);
#else
    std::string error;
    if (!OpenScene(scenePath, error))
    {
        Utility::Printf("Error: the scene file %ws %s\n", scenePath.c_str(), error.c_str());
        // 열지 못하면 원점에서 -z를 본다.
        m_CameraController->SetPose({ Vector3(kZero), 0.0f, 0.0f });
        m_SceneName = GetSceneName(scenePath);
    }
#endif
    EditorCamera& editorCamera = *m_CameraController;
    RegisterPoseCommand(editorCamera);
    ControlChannel::RegisterState("camera", [this] { return DescribeCamera(m_Camera, *m_CameraController); });
    ControlChannel::RegisterState("scene", [this] { return DescribeScene(m_SceneName); });
    ControlChannel::RegisterState("model", [this] { return DescribeModel(m_SceneModel.GetPath(), m_ModelInst); });
    ControlChannel::RegisterState("renderer", DescribeRenderer);
    RegisterSceneCommand();

    SceneEditor::AddObject(m_SceneModel);
    SceneEditor::AddObject(m_Light);
    SceneEditor::AddObject(m_SkyLight);
    SceneEditor::RegisterCommands(m_Camera);
    RegisterLight(m_Light);
    RegisterSkyLight(m_SkyLight);
    RegisterSceneConditions();
    RegisterMeasurementConditions(editorCamera, m_Light, m_SkyLight);
}

void ModelViewer::Cleanup( void )
{
    UnregisterMeasurementConditions();
    UserSettings::Unregister(kSkyLightSettingsSection);
    UserSettings::Unregister(kLightSettingsSection);
    SceneEditor::RemoveObject(m_SkyLight);
    SceneEditor::RemoveObject(m_Light);
    SceneEditor::RemoveObject(m_SceneModel);
    PathTracerTuning::Shutdown();
    RayPick::Shutdown();
    RaytracingView::Shutdown();
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
        // directional light가 어느 방향에 있어도 shadow map이 model 전체를 덮게 bounding sphere를 감싼다.
        // ShadowCamera는 자기 위치에서 light 쪽으로만 depth를 담으므로 위치를 light 반대편 끝에 둔다.
        Scalar ShadowRadius = m_ModelInst.GetRadius();
        m_ShadowCamera.UpdateMatrix(-LightDirection, m_ModelInst.GetCenter() - LightDirection * ShadowRadius, Vector3(ShadowRadius * 2.0f),
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

        MeshSorter sorter(MeshSorter::kDefault);
		sorter.SetCamera(m_Camera);
		sorter.SetViewport(viewport);
		sorter.SetScissor(scissor);
		sorter.SetDepthStencilTarget(g_SceneDepthBuffer);
		sorter.AddRenderTarget(g_SceneColorBuffer);

        m_ModelInst.Render(sorter);

        sorter.Sort();

        {
            ScopedTimer _prof(L"Depth Pre-Pass", gfxContext);
            sorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
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
                shadowSorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
            }

            gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
            gfxContext.ClearColor(g_SceneColorBuffer);

            {
                ScopedTimer _prof(L"Render Color", gfxContext);

                gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
                gfxContext.SetViewportAndScissor(viewport, scissor);

                sorter.RenderMeshes(MeshSorter::kOpaque, gfxContext, globals);
            }

            Renderer::DrawSkyBackground(gfxContext, viewport, scissor, m_SkyLight.GetRadiance());

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
    // raster는 그대로 그린다. ray tracing view의 validation이 raster의 depth를 읽는다. PT의 Shading DDGI Probe Hit는 이 frame의 shadow
    // pass가 그린 shadow map을 읽는다.
    PathTracerTuning::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, m_RaytracingScene, m_Light.GetDirection(), m_Light.GetIlluminance(),
        m_SkyLight.GetRadiance(), g_ShadowBuffer, m_ShadowCamera.GetShadowMatrix(), g_SceneColorBuffer);

    gfxContext.Finish();

    RayPick::Render(m_Camera, m_MainViewport, g_SceneDepthBuffer, m_RaytracingScene);
}

void ModelViewer::RenderSceneOverlay( GraphicsContext& gfxContext )
{
    RaytracingView::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, g_SceneDepthBuffer, m_RaytracingScene, m_Light.GetDirection(), g_SceneOverlayBuffer);
}
