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
#include "Model/Loader/glTF.h"
#include "Model/Renderer/Renderer.h"
#include "Model/Model.h"
#include "Model/Loader/ModelLoader.h"
#include "Core/Camera/ShadowCamera.h"
#include "Core/Display/Display.h"
#include "Core/Editor/EditorCamera.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Automation/ControlChannel.h"

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

// sun은 무한히 멀어 위치가 조명에 영향을 주지 않는다. 위치는 scene editor에서 sun을 보고 고르는 자리다.
class Sun : public SceneEditor::Object
{
public:
    virtual const char* GetName() const override { return "Sun"; }
    virtual Vector3 GetPosition() const override { return m_Position; }
    virtual void SetPosition(Vector3 position) override { m_Position = position; }
    virtual void Draw(const Camera& camera, bool selected) const override;
    virtual void Rotate(const Camera& camera) override;

private:
    Vector3 m_Position = Vector3(kZero);
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
    Sun m_Sun;
};

CREATE_APPLICATION( ModelViewer )

ExpVar g_SunLightIntensity("Viewer/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
NumVar g_SunOrientation("Viewer/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f );
NumVar g_SunInclination("Viewer/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f );

// Sun Inclination 1이 수평에서 올라간 각도(radian). π 대신 3.14159를 쓰므로, sun 명령이 고도를 inclination으로 바꿀 때도 이 값을 써야
// 조명이 쓰는 방향이 요청한 고도가 된다.
constexpr float kSunInclinationToElevation = 3.14159f * 0.5f;

Vector3 GetSunDirection(float orientation, float inclination)
{
    float costheta = cosf(orientation);
    float sintheta = sinf(orientation);
    float cosphi = cosf(inclination * kSunInclinationToElevation);
    float sinphi = sinf(inclination * kSunInclinationToElevation);
    return Normalize(Vector3( costheta * cosphi, sinphi, sintheta * cosphi ));
}

// surface에서 sun을 향하는 방향. raster의 조명과 ray tracing debug view가 같은 sun을 보게 한다.
Vector3 GetSunDirection()
{
    return GetSunDirection(g_SunOrientation, g_SunInclination);
}

// 검증의 Log 한 줄에 찍는 camera와 sun. 같은 화면으로 되돌릴 수 있게 방향으로 바꾸지 않고 저장된 값을 그대로 쓴다.
void DescribeScene(const EditorCamera& camera, RayQueryValidation::SceneConditions& conditions)
{
    const EditorCamera::Pose pose = camera.GetPose();
    char text[96];
    sprintf_s(text, "%.9g,%.9g,%.9g", (float)pose.position.GetX(), (float)pose.position.GetY(), (float)pose.position.GetZ());
    conditions["camera"] = text;
    sprintf_s(text, "%.9g", pose.heading);
    conditions["heading"] = text;
    sprintf_s(text, "%.9g", pose.pitch);
    conditions["pitch"] = text;
    sprintf_s(text, "%.9g", (float)g_SunOrientation);
    conditions["sun orientation"] = text;
    sprintf_s(text, "%.9g", (float)g_SunInclination);
    conditions["sun inclination"] = text;
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

bool RestoreScene(EditorCamera& camera, const RayQueryValidation::SceneConditions& conditions)
{
    float x, y, z, heading, pitch, orientation, inclination;
    if (!ReadCondition(conditions, "camera", x, y, z) || !ReadCondition(conditions, "heading", heading) || !ReadCondition(conditions, "pitch", pitch) ||
        !ReadCondition(conditions, "sun orientation", orientation) || !ReadCondition(conditions, "sun inclination", inclination))
        return false;

    camera.SetPose({ Vector3(x, y, z), heading, pitch });
    g_SunOrientation = orientation;
    g_SunInclination = inclination;
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

float GetSunElevation(Vector3 direction)
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

// 천정에서는 방향이 방위를 정하지 않으므로 Sun Orientation을 방위로 쓴다.
float GetSunAzimuth(Vector3 direction, float orientation)
{
    const float x = direction.GetX();
    const float z = direction.GetZ();
    return WrapDegrees(XMConvertToDegrees(x == 0.0f && z == 0.0f ? orientation : atan2f(z, x)));
}

float GetDegreesBetween(float first, float second)
{
    const float difference = WrapDegrees(first - second);
    return std::min(difference, 360.0f - difference);
}

// state의 sun. 고도와 방위는 조명이 실제로 쓰는 방향에서 계산한다.
ControlChannel::Fields DescribeSun(const Sun& sun)
{
    const Vector3 direction = GetSunDirection();
    return ControlChannel::Fields()
        .Object("direction", DescribeVector(direction))
        .Float("elevation", GetSunElevation(direction))
        .Float("azimuth", GetSunAzimuth(direction, g_SunOrientation))
        .Float("intensity", g_SunLightIntensity)
        .Object("position", DescribeVector(sun.GetPosition()));
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

// 받은 값과 다시 읽은 값이 같다고 보는 차이. 고도와 방위를 radian과 inclination으로 바꾸며 생기는 float 오차보다 크게 잡았다.
// 고도 90은 Sun Inclination의 최댓값 1이 89.99992도라 7.6e-5도 차이로 받는다.
constexpr float kSunDegreesTolerance = 1e-4f;
constexpr float kSunDirectionTolerance = 1e-5f;
constexpr float kSunIntensityTolerance = 1e-6f;

// gizmo와 Engine Tuning이 바꾸는 변수를 같은 방식으로 바꾼다.
// 받지 않는 값이 하나라도 있으면 아무것도 바꾸지 않도록, 변수의 사본에 먼저 적용해 읽어 본다.
void SetSun(Sun& sun, const std::string& arguments, const ControlChannel::Reply& reply)
{
    const std::string usage = "usage: sun set elevation=<degrees> azimuth=<degrees> | direction=<x>,<y>,<z> | position=<x>,<y>,<z> | intensity=<v>";
    std::map<std::string, std::vector<float>> values;
    std::istringstream tokens(arguments);
    for (std::string token; tokens >> token;)
    {
        const size_t separator = token.find('=');
        const std::string key = token.substr(0, separator);
        const int count = key == "direction" || key == "position" ? 3 : key == "elevation" || key == "azimuth" || key == "intensity" ? 1 : 0;
        if (separator == std::string::npos || count == 0 || values.count(key) != 0)
        {
            reply.Error("'" + token + "' is not a key=value of sun set, or repeats a key. Nothing was set. " + usage);
            return;
        }
        float numbers[3];
        if (!ReadNumbers(token.substr(separator + 1), numbers, count))
        {
            reply.Error(key + " takes " + (count == 3 ? "<x>,<y>,<z>" : "a number") + ". Nothing was set. " + usage);
            return;
        }
        values[key].assign(numbers, numbers + count);
    }
    if (values.empty())
    {
        reply.Error(usage);
        return;
    }
    const bool hasElevation = values.count("elevation") != 0;
    const bool hasAzimuth = values.count("azimuth") != 0;
    const bool hasDirection = values.count("direction") != 0;
    if (hasDirection && (hasElevation || hasAzimuth))
    {
        reply.Error("direction sets elevation and azimuth, so it cannot be given with them. Nothing was set.");
        return;
    }

    NumVar orientation = g_SunOrientation;
    NumVar inclination = g_SunInclination;
    ExpVar intensity = g_SunLightIntensity;
    Vector3 position = sun.GetPosition();
    Vector3 direction(kZero);
    if (hasElevation)
        inclination = XMConvertToRadians(values["elevation"][0]) / kSunInclinationToElevation;
    if (hasAzimuth)
        orientation = XMConvertToRadians(WrapDegrees(values["azimuth"][0]));
    if (hasDirection)
    {
        const std::vector<float>& xyz = values["direction"];
        const Vector3 requested(xyz[0], xyz[1], xyz[2]);
        if (LengthSquare(requested) == 0.0f)
        {
            reply.Error("direction has no length. Nothing was set.");
            return;
        }
        direction = Normalize(requested);
        inclination = asinf(Clamp((float)direction.GetY(), -1.0f, 1.0f)) / kSunInclinationToElevation;
        if (direction.GetX() != 0.0f || direction.GetZ() != 0.0f)
            orientation = atan2f(direction.GetZ(), direction.GetX());
    }
    if (values.count("position") != 0)
        position = Vector3(values["position"][0], values["position"][1], values["position"][2]);
    // ExpVar는 log2로 저장하므로 0 이하는 NaN이 된다. /fp:fast에서는 NaN과의 비교를 믿을 수 없어 사본에 넣기 전에 거른다.
    const bool hasIntensity = values.count("intensity") != 0;
    const bool intensityPositive = !hasIntensity || values["intensity"][0] > 0.0f;
    if (hasIntensity && intensityPositive)
        intensity = values["intensity"][0];

    const Vector3 applied = GetSunDirection(orientation, inclination);
    const float appliedElevation = GetSunElevation(applied);
    const float appliedAzimuth = GetSunAzimuth(applied, orientation);
    ControlChannel::Fields rejected;
    bool anyRejected = false;
    if (hasElevation && fabsf(appliedElevation - values["elevation"][0]) > kSunDegreesTolerance)
    {
        rejected.Float("elevation", appliedElevation);
        anyRejected = true;
    }
    if (hasAzimuth && GetDegreesBetween(appliedAzimuth, values["azimuth"][0]) > kSunDegreesTolerance)
    {
        rejected.Float("azimuth", appliedAzimuth);
        anyRejected = true;
    }
    const Vector3 directionError = Abs(applied - direction);
    if (hasDirection && std::max({ (float)directionError.GetX(), (float)directionError.GetY(), (float)directionError.GetZ() }) > kSunDirectionTolerance)
    {
        rejected.Object("direction", DescribeVector(applied));
        anyRejected = true;
    }
    if (hasIntensity && (!intensityPositive || fabsf(intensity - values["intensity"][0]) > kSunIntensityTolerance * values["intensity"][0]))
    {
        rejected.Float("intensity", intensity);
        anyRejected = true;
    }
    if (anyRejected)
    {
        reply.Error("the sun cannot take some values, so nothing was set. rejected has what they would read back as. "
            "elevation is 0 to 90, so direction needs y >= 0, and intensity is limited to the range of Sun Light Intensity.",
            ControlChannel::Fields().Object("rejected", rejected));
        return;
    }

    g_SunOrientation = orientation;
    g_SunInclination = inclination;
    // ExpVar는 지수를 저장하므로 값을 넣지 않고 사본을 그대로 옮긴다. 그래야 바꾸지 않은 intensity가 bit 단위로 그대로 남는다.
    g_SunLightIntensity = intensity;
    sun.SetPosition(position);

    ControlChannel::Fields fields;
    const bool toZenith = (hasElevation || hasDirection) && appliedElevation >= 90.0f - kSunDegreesTolerance;
    if (toZenith && !hasAzimuth)
        fields.Text("note", "at the zenith the direction has no azimuth, so azimuth stays as it was");
    reply.Ok(fields.Object("state", ControlChannel::GetState("sun")));
}

void RegisterSunCommand(Sun& sun)
{
    ControlChannel::RegisterState("sun", [&sun] { return DescribeSun(sun); });
    ControlChannel::Register("sun", "sun set <key>=<value> ...: the sun, as its rotate and translate gizmos and Engine Tuning's Viewer/Lighting change it. "
        "elevation=<degrees> is up from the horizon, 0 to 90. azimuth=<degrees> turns about +y from +x toward +z, 0 to 360. "
        "direction=<x>,<y>,<z> points from a surface toward the sun and is normalized. It sets elevation and azimuth, so it is not given with them. "
        "position=<x>,<y>,<z> in world units only places the icon, as the sun is infinitely far. intensity=<v> is Sun Light Intensity. "
        "Nothing is set unless every value reads back as given. state sun has these keys, with the direction the lighting uses",
        [&sun](const std::string& arguments, const ControlChannel::Reply& reply)
    {
        if (arguments.compare(0, 4, "set ") != 0)
        {
            reply.Error("usage: sun set <key>=<value> ... . state sun reads the sun");
            return;
        }
        SetSun(sun, arguments.substr(4), reply);
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

void Sun::Draw(const Camera& camera, bool selected) const
{
    ImVec2 icon;
    if (!SceneEditor::ProjectToScene(camera, m_Position, icon))
        return;

    const ImU32 sunColor = IM_COL32(255, 200, 0, 255);
    const ImU32 selectedColor = IM_COL32(255, 128, 16, 255);
    const float radius = SceneEditor::GetIconRadius();
    const float thickness = radius * 0.15f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 화살표는 빛이 나아가는 방향이다.
    const float arrowLength = radius * 4.0f * SceneEditor::GetWorldLengthPerPixel(camera, m_Position);
    ImVec2 arrowTip;
    if (SceneEditor::ProjectToScene(camera, m_Position - GetSunDirection() * arrowLength, arrowTip))
        DrawArrow(*drawList, icon, arrowTip, sunColor, thickness);

    drawList->AddCircleFilled(icon, radius * 0.45f, sunColor);
    const int kRayCount = 8;
    for (int i = 0; i < kRayCount; ++i)
    {
        const float angle = XM_2PI * i / kRayCount;
        const ImVec2 direction(cosf(angle), sinf(angle));
        drawList->AddLine(
            ImVec2(icon.x + direction.x * radius * 0.65f, icon.y + direction.y * radius * 0.65f),
            ImVec2(icon.x + direction.x * radius, icon.y + direction.y * radius),
            sunColor, thickness);
    }

    if (selected)
        drawList->AddCircle(icon, radius * 1.2f, selectedColor, 0, thickness);
}

void Sun::Rotate(const Camera& camera)
{
    // 이 frame의 Y ring은 수평이라 sun의 방위를, X ring은 천정과 sun을 지나는 수직 원이라 sun의 고도를 바꾼다.
    const float orientation = g_SunOrientation;
    const float elevation = g_SunInclination * XM_PIDIV2;
    Matrix4 frame(
        Vector3(sinf(orientation), 0.0f, -cosf(orientation)),
        Vector3(kYUnitVector),
        Vector3(cosf(orientation), 0.0f, sinf(orientation)),
        m_Position);
    const Vector3 sunInFrame(0.0f, sinf(elevation), cosf(elevation));

    if (!SceneEditor::Manipulate(camera, ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y, ImGuizmo::LOCAL, frame))
        return;

    const Vector3 sun = frame.Get3x3() * sunInFrame;
    const float x = sun.GetX(), y = sun.GetY(), z = sun.GetZ();
    g_SunInclination = asinf(Clamp(y, -1.0f, 1.0f)) / XM_PIDIV2;
    // 천정에서는 방위가 정해지지 않으므로 그대로 둔다.
    if (sqrtf(x * x + z * z) > 1e-4f)
        g_SunOrientation = atan2f(z, x);
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
            [editorCamera](RayQueryValidation::SceneConditions& conditions) { DescribeScene(*editorCamera, conditions); },
            [editorCamera](const RayQueryValidation::SceneConditions& conditions) { return RestoreScene(*editorCamera, conditions); },
            { "camera", "sun" });
    }
    else
        m_CameraController.reset(new OrbitCamera(m_Camera, m_ModelInst.GetBoundingSphere(), Vector3(kYUnitVector)));
    RegisterPoseCommand(editorCamera);
    ControlChannel::RegisterState("camera", [this, editorCamera] { return DescribeCamera(m_Camera, editorCamera); });
    const std::string modelPath = Utility::ToUTF8(gltfFileName.empty() ? std::wstring(kDefaultModel) : gltfFileName);
    ControlChannel::RegisterState("model", [this, modelPath] { return DescribeModel(modelPath, m_ModelInst); });

    SceneEditor::AddObject(m_Sun);
    SceneEditor::RegisterCommands(m_Camera);
    RegisterSunCommand(m_Sun);
}

void ModelViewer::Cleanup( void )
{
    SceneEditor::RemoveObject(m_Sun);
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
        Vector3 LightDirection = GetSunDirection();
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
        globals.LightIntensity = Vector3(Scalar(g_SunLightIntensity));

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
    PathTracerPanel::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, m_RaytracingScene, g_SceneColorBuffer);

    gfxContext.Finish();

    RayPick::Render(m_Camera, m_MainViewport, g_SceneDepthBuffer, m_RaytracingScene);
}

void ModelViewer::RenderSceneOverlay( GraphicsContext& gfxContext )
{
    RaytracingView::Render(gfxContext.GetComputeContext(), m_Camera, m_MainViewport, g_SceneDepthBuffer, m_RaytracingScene, GetSunDirection(), g_SceneOverlayBuffer);
}
