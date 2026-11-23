#include "Core/pch.h"
#include "ModelViewer/Light.h"
#include "Core/Automation/VectorFields.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Math/Rotation.h"
#include "Core/Scene/SettingValues.h"

#include <algorithm>
#include <map>
#include <sstream>

using namespace Math;

class LightIconComponent : public SceneEditor::IconComponent
{
public:
    explicit LightIconComponent(LightComponent& light) : m_Light(light) {}

    virtual Vector3 GetPosition() const override { return m_Light.GetProperties().position; }
    virtual void SetPosition(Vector3 position) override;
    virtual void Draw(const Camera& camera, bool selected) const override;
    virtual void Rotate(const Camera& camera, ImGuizmo::MODE axes) override;
    virtual void Scale(const Camera&) override {}

private:
    LightComponent& m_Light;
};

constexpr LightComponent::Type kDefaultLightType = LightComponent::Type::Directional;
// 예전에 방위 -0.5 rad와 고도 0.75 x 90°로 정하던 기본 방향을 bit 단위로 옮겨서, 기본 화면이 그때와 같다.
const Vector3 kDefaultLightDirection(0.335837096f, 0.923879147f, -0.18346864f);

Matrix3 MakeRotationToward(Vector3 direction)
{
    const Vector3 up = fabsf(direction.GetY()) > 0.9999f ? Vector3(kZUnitVector) : Vector3(kYUnitVector);
    const Vector3 x = Normalize(Cross(up, direction));
    return Matrix3(x, Cross(direction, x), direction);
}

// 두 vector를 그대로 쓰므로, 저장한 rotation의 두 vector를 다시 읽으면 bit 단위로 같은 rotation이 된다.
bool MakeRotation(Vector3 direction, Vector3 up, Matrix3& rotation)
{
    const float tolerance = 1e-4f;
    if (fabsf(Length(direction) - 1.0f) > tolerance || fabsf(Length(up) - 1.0f) > tolerance || fabsf(Dot(direction, up)) > tolerance)
        return false;
    rotation = Matrix3(Cross(up, direction), up, direction);
    return true;
}

LightComponent::Properties LightComponent::GetDefaultProperties()
{
    Properties properties;
    properties.rotation = MakeRotationToward(kDefaultLightDirection);
    properties.position = Vector3(kZero);
    properties.color = Vector3(1.0f, 1.0f, 1.0f);
    properties.intensity = kDefaultLightIntensity;
    return properties;
}

LightComponent::LightComponent()
    : Component({ "Type", "Direction", "Up", "Position", "Color", "Intensity" }), m_Type(kDefaultLightType), m_Properties(GetDefaultProperties())
{
}

void LightComponent::ResetTransform()
{
    m_Properties.rotation = Matrix3(kIdentity);
    m_Properties.position = Vector3(kZero);
}

bool IsColor(Vector3 color)
{
    const Vector3 clamped = Min(Max(color, Vector3(kZero)), Vector3(1.0f, 1.0f, 1.0f));
    return XMVector3Equal(color, clamped);
}

const std::pair<LightComponent::Type, const char*> kLightTypeNames[] = { { LightComponent::Type::Directional, "Directional" } };

const char* GetLightTypeName(LightComponent::Type type)
{
    for (const auto& name : kLightTypeNames)
    {
        if (name.first == type)
            return name.second;
    }
    ASSERT(false, "kLightTypeNames has no name for a light type.");
    return "";
}

bool ReadLightType(const std::string& text, LightComponent::Type& type)
{
    for (const auto& name : kLightTypeNames)
    {
        if (text == name.second)
        {
            type = name.first;
            return true;
        }
    }
    return false;
}

void LightComponent::SaveSettings(IniFile::Values& values) const
{
    values["Type"] = GetLightTypeName(m_Type);
    values["Direction"] = SettingValues::FormatVector(m_Properties.rotation.GetZ());
    values["Up"] = SettingValues::FormatVector(m_Properties.rotation.GetY());
    values["Position"] = SettingValues::FormatVector(m_Properties.position);
    values["Color"] = SettingValues::FormatVector(m_Properties.color);
    char text[32];
    sprintf_s(text, "%.9g", m_Properties.intensity);
    values["Intensity"] = text;
}

void LightComponent::LoadSettings(const IniFile::Values& values)
{
    const auto read = [&values](const char* key, Vector3& vector)
    {
        const auto text = values.find(key);
        return text != values.end() && SettingValues::ReadVector(text->second, vector);
    };

    Type type = kDefaultLightType;
    const auto typeName = values.find("Type");
    if (typeName != values.end())
        ReadLightType(typeName->second, type);

    Properties properties = GetDefaultProperties();
    Vector3 direction, up, position, color;
    if (read("Direction", direction) && read("Up", up))
        MakeRotation(direction, up, properties.rotation);
    if (read("Position", position))
        properties.position = position;
    if (read("Color", color) && IsColor(color))
        properties.color = color;
    const auto intensity = values.find("Intensity");
    float number = 0.0f;
    if (intensity != values.end() && SettingValues::ReadNumbers(intensity->second, &number, 1) && number >= 0.0f)
        properties.intensity = number;
    m_Type = type;
    m_Properties = properties;
}

void DescribeLightConditions(const LightComponent& light, MeasurementConditions::Values& values)
{
    const LightComponent::Properties& properties = light.GetProperties();
    values["light direction"] = SettingValues::FormatVector(properties.rotation.GetZ());
    values["light up"] = SettingValues::FormatVector(properties.rotation.GetY());
    values["light color"] = SettingValues::FormatVector(properties.color);
    values["light intensity"] = MeasurementConditions::FormatFloat(properties.intensity);
}

void ReadLightConditions(LightComponent& light, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    LightComponent::Properties properties = light.GetProperties();
    Vector3 direction, up;
    const bool hasDirection = MeasurementConditions::ReadValue(values, "light direction", reading, [&direction](const std::string& text) { return SettingValues::ReadVector(text, direction); });
    const bool hasUp = MeasurementConditions::ReadValue(values, "light up", reading, [&up](const std::string& text) { return SettingValues::ReadVector(text, up); });
    if (hasDirection && hasUp && !MakeRotation(direction, up, properties.rotation))
    {
        reading.unread.push_back("light direction");
        reading.unread.push_back("light up");
    }
    MeasurementConditions::ReadValue(values, "light color", reading, [&properties](const std::string& text) { return SettingValues::ReadVector(text, properties.color) && IsColor(properties.color); });
    MeasurementConditions::ReadValue(values, "light intensity", reading, [&properties](const std::string& text)
    {
        return SettingValues::ReadNumbers(text, &properties.intensity, 1) && properties.intensity >= 0.0f;
    });
    reading.applies.push_back([&light, properties] { light.SetProperties(properties); });
}

float GetElevation(Vector3 direction)
{
    return XMConvertToDegrees(asinf(Clamp((float)direction.GetY(), -1.0f, 1.0f)));
}

float WrapDegrees(float degrees)
{
    const float wrapped = fmodf(degrees, 360.0f);
    if (wrapped >= 0.0f)
        return wrapped;
    // -0.00001처럼 360을 더하면 360으로 반올림되는 값은 0이다.
    return wrapped + 360.0f < 360.0f ? wrapped + 360.0f : 0.0f;
}

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

Vector3 GetDirectionAt(float elevation, float azimuth)
{
    if (fabsf(elevation) == 90.0f)
        return Vector3(0.0f, elevation > 0.0f ? 1.0f : -1.0f, 0.0f);
    const float e = XMConvertToRadians(elevation);
    const float a = XMConvertToRadians(azimuth);
    return Normalize(Vector3(cosf(e) * cosf(a), sinf(e), cosf(e) * sinf(a)));
}

ControlChannel::Fields DescribeLight(const LightComponent& light)
{
    const LightComponent::Properties& properties = light.GetProperties();
    const Quaternion rotation = GetCanonicalQuaternion(Quaternion((XMMATRIX)properties.rotation));
    const Vector3 direction = light.GetDirection();
    return ControlChannel::Fields()
        .Object("rotation", ControlChannel::Fields()
            .Float("x", XMVectorGetX(rotation)).Float("y", XMVectorGetY(rotation)).Float("z", XMVectorGetZ(rotation)).Float("w", XMVectorGetW(rotation)))
        .Object("direction", ControlChannel::DescribeVector(direction))
        .Float("elevation", GetElevation(direction))
        .Float("azimuth", GetAzimuth(direction))
        .Object("color", ControlChannel::Fields()
            .Float("r", properties.color.GetX()).Float("g", properties.color.GetY()).Float("b", properties.color.GetZ()))
        .Float("intensity", properties.intensity)
        .Object("position", ControlChannel::DescribeVector(properties.position));
}

// 받은 값과 다시 읽은 고도와 방위가 같다고 보는 차이. degree와 radian을 오가며 생기는 float 오차보다 크게 잡았다.
constexpr float kLightDegreesTolerance = 1e-4f;

void SetLight(LightComponent& light, const std::string& arguments, const ControlChannel::Reply& reply)
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
        if (!SettingValues::ReadNumbers(token.substr(separator + 1), numbers, count->second))
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

    LightComponent::Properties properties = light.GetProperties();
    ControlChannel::Fields rejected;
    bool anyRejected = false;
    if (values.count("rotation") != 0)
    {
        const std::vector<float>& xyzw = values["rotation"];
        Quaternion rotation;
        if (!MakeQuaternionRotation(xyzw[0], xyzw[1], xyzw[2], xyzw[3], rotation))
        {
            reply.Error("rotation has no length. Nothing was set.");
            return;
        }
        properties.rotation = Matrix3(rotation);
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

void RegisterLight(LightComponent& light)
{
    ControlChannel::RegisterState("light", [&light] { return DescribeLight(light); });
    ControlChannel::Register("light", "light set <key>=<value> ... | light reset: the directional light, as its rotate and translate gizmos and its "
        "Details panel change it. reset sets the transform to no rotation at the origin, so the light goes along world -Z, "
        "and keeps the color and intensity, as Reset Transform in the Details panel does. It is a KHR_lights_punctual directional light and goes along the local -Z of its transform. "
        "Its Type, which editor get gives and the scene file keeps, is Directional, the only type for now, and light set does not change it. "
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

void LightIconComponent::SetPosition(Vector3 position)
{
    LightComponent::Properties properties = m_Light.GetProperties();
    properties.position = position;
    m_Light.SetProperties(properties);
}

void LightIconComponent::Draw(const Camera& camera, bool selected) const
{
    const Vector3 position = GetPosition();
    ImVec2 icon;
    if (!SceneEditor::ProjectToScene(camera, position, icon))
        return;

    const ImU32 lightColor = IM_COL32(255, 200, 0, 255);
    const ImU32 selectedColor = IM_COL32(255, 128, 16, 255);
    const float radius = SceneEditor::GetIconRadius();
    const float thickness = radius * 0.15f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float arrowLength = radius * 4.0f * SceneEditor::GetWorldLengthPerPixel(camera, position);
    ImVec2 arrowTip;
    if (SceneEditor::ProjectToScene(camera, position - m_Light.GetDirection() * arrowLength, arrowTip))
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

void LightIconComponent::Rotate(const Camera& camera, ImGuizmo::MODE axes)
{
    LightComponent::Properties properties = m_Light.GetProperties();
    Matrix4 transform(properties.rotation, properties.position);
    if (!SceneEditor::Manipulate(camera, ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z, axes, transform))
        return;
    // gizmo가 돌린 matrix는 float 오차로 직교에서 조금씩 벗어나므로 quaternion을 거쳐 직교 matrix로 되돌린다.
    properties.rotation = Matrix3(Normalize(Quaternion((XMMATRIX)transform.Get3x3())));
    m_Light.SetProperties(properties);
}

void ShowLightDetails(LightComponent& light)
{
    ImGui::LabelText("Type", "%s", GetLightTypeName(light.GetType()));
    ImGui::SetItemTooltip("KHR_lights_punctual type of the light, which the scene file keeps as Type. Directional is the only type "
        "the renderer supports now.");

    LightComponent::Properties properties = light.GetProperties();
    float color[3] = { properties.color.GetX(), properties.color.GetY(), properties.color.GetZ() };
    if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_Float))
        properties.color = Min(Max(Vector3(color[0], color[1], color[2]), Vector3(kZero)), Vector3(1.0f, 1.0f, 1.0f));
    ImGui::SetItemTooltip("Linear RGB, each 0 to 1.");
    if (ImGui::DragFloat("Intensity", &properties.intensity, 0.05f, 0.0f, FLT_MAX, "%.3f lux"))
        properties.intensity = std::max(0.0f, properties.intensity);
    ImGui::SetItemTooltip("Illuminance on a surface facing the light, in lux.");
    light.SetProperties(properties);

    ImGui::SeparatorText("Transform");
    const Vector3 direction = light.GetDirection();
    ImGui::Text("Toward the light: %.3f, %.3f, %.3f", (float)direction.GetX(), (float)direction.GetY(), (float)direction.GetZ());
    ImGui::Text("Elevation %.2f, azimuth %.2f degrees", GetElevation(direction), GetAzimuth(direction));
    if (ImGui::Button("Reset Transform"))
        light.ResetTransform();
    ImGui::SetItemTooltip("Sets the transform to no rotation at the origin, so the light goes along world -Z. The color and intensity stay.");
    ImGui::TextDisabled("Rotate the light with the gizmo (E). The toolbar on the scene picks local or world rings.");
}

LightComponent& AssembleLight(Scene::Object& object)
{
    LightComponent& light = object.AddComponent<LightComponent>();
    object.AddComponent<LightIconComponent>(light);
    return light;
}
