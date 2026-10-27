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
#include "Model/Raytracing/RaytracingPanel.h"
#include "Model/Raytracing/RayQueryValidation.h"
#include "Model/Raytracing/RayPick.h"
#include "Model/PathTracer/PathTracerPanel.h"
#include "Model/PathTracer/BrdfTest.h"
#include "Model/Loader/glTF.h"
#include "Model/Renderer/Renderer.h"
#include "Model/Model.h"
#include "Model/Loader/ModelLoader.h"
#include "Core/Camera/ShadowCamera.h"
#include "Core/Display/Display.h"
#include "Core/Editor/EditorCamera.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/EditorSettings.h"
#include "Core/UI/ImGuiLayer.h"

#include <algorithm>
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
class DirectionalLight : public SceneEditor::Object
{
public:
    // gizmo, light set 명령, panel, editor.ini가 바꾸는 값
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

    // View 메뉴의 Directional Light panel. color와 intensity를 바꾸고 transform을 되돌린다.
    void ShowPanel();

    virtual const char* GetName() const override { return "Directional Light"; }
    virtual Vector3 GetPosition() const override { return m_Properties.position; }
    virtual void SetPosition(Vector3 position) override { m_Properties.position = position; }
    virtual void Draw(const Camera& camera, bool selected) const override;
    virtual void Rotate(const Camera& camera, ImGuizmo::MODE axes) override;

private:
    Properties m_Properties;
    bool m_ShowPanel = false;
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

    Camera m_Camera;
    unique_ptr<CameraController> m_CameraController;

    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT m_MainScissor;

    ModelInstance m_ModelInst;
    RaytracingScene m_RaytracingScene;
    ShadowCamera m_ShadowCamera;
    DirectionalLight m_Light;
};

CREATE_APPLICATION( ModelViewer )

const char kLightSettingsSection[] = "Directional Light";

// 예전에 방위 -0.5 rad와 고도 0.75 x 90°로 정하던 기본 방향을 bit 단위로 옮겨서, 기본 화면이 그때와 같다.
const Vector3 kDefaultLightDirection(0.335837096f, 0.923879147f, -0.18346864f);
// spec의 기본 intensity는 1 lux이지만, 기본 화면과 harness baseline의 Exposure가 맞춰 둔 밝기를 지키도록 4π로 둔다. 그 Exposure를
// 맞출 때의 raster는 diffuse에 1/π가 없어 intensity 4로 지금의 4π만큼 밝았다.
constexpr float kDefaultLightIntensity = 4.0f * XM_PI;

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

void SaveLightSettings(const DirectionalLight& light, EditorSettings::Values& values)
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
void LoadLightSettings(DirectionalLight& light, const EditorSettings::Values& values)
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

bool ReadCondition(const RayQueryValidation::SceneConditions& conditions, const char* key, float& value)
{
    const auto text = conditions.find(key);
    return text != conditions.end() && sscanf_s(text->second.c_str(), "%f", &value) == 1;
}

bool ReadCondition(const RayQueryValidation::SceneConditions& conditions, const char* key, float& x, float& y, float& z)
{
    const auto text = conditions.find(key);
    return text != conditions.end() && sscanf_s(text->second.c_str(), "%f,%f,%f", &x, &y, &z) == 3;
}

// 검증의 Log 한 줄에 찍는 camera와 directional light. 같은 화면으로 되돌릴 수 있게 저장된 값을 그대로 쓴다.
// light의 rotation은 editor.ini처럼 +Z와 +Y로 적는다.
void DescribeScene(const EditorCamera& camera, const DirectionalLight& light, RayQueryValidation::SceneConditions& conditions)
{
    const EditorCamera::Pose pose = camera.GetPose();
    char text[96];
    sprintf_s(text, "%.9g,%.9g,%.9g", (float)pose.position.GetX(), (float)pose.position.GetY(), (float)pose.position.GetZ());
    conditions["camera"] = text;
    sprintf_s(text, "%.9g", pose.heading);
    conditions["heading"] = text;
    sprintf_s(text, "%.9g", pose.pitch);
    conditions["pitch"] = text;
    const Matrix3& rotation = light.GetProperties().rotation;
    conditions["light direction"] = FormatVector(rotation.GetZ());
    conditions["light up"] = FormatVector(rotation.GetY());
}

bool RestoreScene(EditorCamera& camera, DirectionalLight& light, const RayQueryValidation::SceneConditions& conditions)
{
    float x, y, z, heading, pitch;
    float direction[3], up[3];
    DirectionalLight::Properties properties = light.GetProperties();
    if (!ReadCondition(conditions, "camera", x, y, z) || !ReadCondition(conditions, "heading", heading) || !ReadCondition(conditions, "pitch", pitch) ||
        !ReadCondition(conditions, "light direction", direction[0], direction[1], direction[2]) ||
        !ReadCondition(conditions, "light up", up[0], up[1], up[2]) ||
        !MakeRotation(Vector3(direction[0], direction[1], direction[2]), Vector3(up[0], up[1], up[2]), properties.rotation))
        return false;

    camera.SetPose({ Vector3(x, y, z), heading, pitch });
    light.SetProperties(properties);
    return true;
}

ControlChannel::Fields DescribeVector(Vector3 vector)
{
    return ControlChannel::Fields().Float("x", vector.GetX()).Float("y", vector.GetY()).Float("z", vector.GetZ());
}

// state의 camera. editorCamera가 없으면 OrbitCamera라 pose가 없다.
ControlChannel::Fields DescribeCamera(const Camera& camera, const EditorCamera* editorCamera)
{
    const Vector3 position = camera.GetPosition();
    ControlChannel::Fields fields;
    fields.Text("controller", editorCamera != nullptr ? "editor" : "orbit")
        .Float("x", position.GetX())
        .Float("y", position.GetY())
        .Float("z", position.GetZ());
    if (editorCamera != nullptr)
    {
        const EditorCamera::Pose pose = editorCamera->GetPose();
        fields.Float("heading", pose.heading).Float("pitch", pose.pitch).Float("move_speed", editorCamera->GetMoveSpeed());
    }
    return fields
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

// harness가 camera를 숫자로 읽고 정한다. editorCamera가 없으면 OrbitCamera라 pose를 정할 수 없다.
void RegisterPoseCommand(EditorCamera* editorCamera)
{
    ControlChannel::SetStatus("camera", editorCamera != nullptr ? "editor" : "orbit");
    ControlChannel::Register("pose", "pose | pose <x> <y> <z> <heading> <pitch> | pose look-at <x> <y> <z>: the editor camera, "
        "in world units and radians as in state camera. look-at keeps the position and turns the camera toward the point. "
        "Looking straight up or down keeps the heading", [editorCamera](const std::string& arguments, const ControlChannel::Reply& reply)
    {
        if (editorCamera == nullptr)
        {
            reply.Error("pose needs the editor camera. -model uses the orbit camera.");
            return;
        }

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
            if (LengthSquare(target - editorCamera->GetPose().position) == 0.0f)
            {
                reply.Error("the point is where the camera is, so there is no direction to look in");
                return;
            }
            editorCamera->LookAt(target);
        }
        else if (!arguments.empty())
        {
            if (sscanf_s(arguments.c_str(), "%f %f %f %f %f", &x, &y, &z, &heading, &pitch) != 5)
            {
                reply.Error(usage);
                return;
            }
            editorCamera->SetPose({ Vector3(x, y, z), heading, pitch });
        }

        const EditorCamera::Pose pose = editorCamera->GetPose();
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

// gizmo와 panel이 바꾸는 값을 같은 방식으로 바꾼다.
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

// light를 editor.ini의 section, harness의 light 명령과 state로 읽고 바꾸게 한다.
void RegisterLight(DirectionalLight& light)
{
    EditorSettings::Register(kLightSettingsSection,
        [&light](EditorSettings::Values& values) { SaveLightSettings(light, values); },
        [&light](const EditorSettings::Values& values) { LoadLightSettings(light, values); });
    ControlChannel::RegisterState("light", [&light] { return DescribeLight(light); });
    ControlChannel::Register("light", "light set <key>=<value> ... | light reset: the directional light, as its rotate and translate gizmos and the "
        "Directional Light panel change it. reset sets the transform to no rotation at the origin, so the light goes along world -Z, "
        "and keeps the color and intensity, as Reset Transform in the panel does. It is a KHR_lights_punctual directional light and goes along the local -Z of its transform. "
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

void DirectionalLight::ShowPanel()
{
    ImGuiLayer::ShowPanel("Directional Light", m_ShowPanel, [this]
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
        ImGui::TextDisabled("Select the light's icon and rotate it with the gizmo (E). The Scene Editor panel picks local or world rings.");
    });
}

// scripts/viewer.py가 이 이름과 형식으로 기본 model 경로를 읽는다.
const wchar_t kDefaultModel[] = L"glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf";

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
    PathTracerPanel::Initialize();
    BrdfTest::Initialize();

    std::wstring gltfFileName;

    bool forceRebuild = false;
    uint32_t rebuildValue;
    if (CommandLineArgs::GetInteger(L"rebuild", rebuildValue))
        forceRebuild = rebuildValue != 0;

    if (CommandLineArgs::GetString(L"model", gltfFileName) == false)
    {
#ifdef LEGACY_RENDERER
        Sponza::Startup(m_Camera);
#else
        m_ModelInst = Renderer::LoadModel(kDefaultModel, forceRebuild);
        // 옛 Sponza(DirectX-Graphics-Samples의 sponza2.gltf)로 정한 camera pose와 검증 Log 줄을 그대로 쓰도록,
        // 옛 Sponza를 100배 하던 때와 world 좌표가 같아지는 배율이다 (Khronos 판은 node scale을 빼면 옛 판의 100배이고, node scale 0.008 × 125 ≈ 1).
        m_ModelInst.Resize(125.0f * m_ModelInst.GetRadius());
        OrientedBox obb = m_ModelInst.GetBoundingBox();
        float modelRadius = Length(obb.GetDimensions()) * 0.5f;
        const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
        m_Camera.SetEyeAtUp( eye, Vector3(kZero), Vector3(kYUnitVector) );
#endif
    }
    else
    {
        m_ModelInst = Renderer::LoadModel(gltfFileName, forceRebuild);
        m_ModelInst.LoopAllAnimations();
        m_ModelInst.Resize(10.0f);

        MotionBlur::Enable = false;
    }

    m_Camera.SetZRange(1.0f, 10000.0f);
    EditorCamera* editorCamera = nullptr;
    if (gltfFileName.size() == 0)
    {
        editorCamera = new EditorCamera(m_Camera, Vector3(kYUnitVector));
        m_CameraController.reset(editorCamera);
        // OrbitCamera는 pose를 정할 수 없으므로 검증의 Restore는 EditorCamera를 쓸 때만 된다.
        RayQueryValidation::SetSceneConditions(
            [this, editorCamera](RayQueryValidation::SceneConditions& conditions) { DescribeScene(*editorCamera, m_Light, conditions); },
            [this, editorCamera](const RayQueryValidation::SceneConditions& conditions) { return RestoreScene(*editorCamera, m_Light, conditions); },
            { "camera", "light" });
    }
    else
        m_CameraController.reset(new OrbitCamera(m_Camera, m_ModelInst.GetBoundingSphere(), Vector3(kYUnitVector)));
    RegisterPoseCommand(editorCamera);
    ControlChannel::RegisterState("camera", [this, editorCamera] { return DescribeCamera(m_Camera, editorCamera); });
    const std::string modelPath = Utility::ToUTF8(gltfFileName.empty() ? std::wstring(kDefaultModel) : gltfFileName);
    ControlChannel::RegisterState("model", [this, modelPath] { return DescribeModel(modelPath, m_ModelInst); });

    SceneEditor::AddObject(m_Light);
    SceneEditor::RegisterCommands(m_Camera);
    RegisterLight(m_Light);
}

void ModelViewer::Cleanup( void )
{
    EditorSettings::Unregister(kLightSettingsSection);
    SceneEditor::RemoveObject(m_Light);
    PathTracerPanel::Shutdown();
    RayPick::Shutdown();
    RaytracingView::Shutdown();
    m_RaytracingScene.Destroy();
    m_ModelInst = nullptr;

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

    if (GameInput::IsFirstPressed(GameInput::kLShoulder))
        DebugZoom.Decrement();
    else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
        DebugZoom.Increment();

    // scene buffer 크기는 scene viewport를 따라 frame마다 바뀔 수 있다.
    m_Camera.SetAspectRatio((float)g_SceneColorBuffer.GetHeight() / g_SceneColorBuffer.GetWidth());
    m_CameraController->Update(deltaT);

    SceneEditor::Update(m_Camera);
    m_Light.ShowPanel();
    RaytracingPanel::Update();
    PathTracerPanel::Update();

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
    // raster는 그대로 그린다. ray tracing view의 validation이 raster의 depth를 읽는다.
    PathTracerPanel::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, m_RaytracingScene, m_Light.GetDirection(), m_Light.GetIlluminance(),
        g_SceneColorBuffer);

    gfxContext.Finish();

    RayPick::Render(m_Camera, m_MainViewport, g_SceneDepthBuffer, m_RaytracingScene);
}

void ModelViewer::RenderSceneOverlay( GraphicsContext& gfxContext )
{
    RaytracingView::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, g_SceneDepthBuffer, m_RaytracingScene, m_Light.GetDirection(), g_SceneOverlayBuffer);
}
