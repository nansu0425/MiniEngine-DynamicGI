#include "Core/pch.h"
#include "ModelViewer/Light.h"
#include "Core/Automation/VectorFields.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Math/Rotation.h"
#include "Core/Scene/SettingValues.h"
#include "Model/Model.h"

#include <algorithm>
#include <map>
#include <sstream>

using namespace Math;

class LightIconComponent : public SceneEditor::IconComponent
{
public:
    LightIconComponent(LightComponent& light, const ModelInstance& model) : m_Light(light), m_Model(model) {}

    virtual Vector3 GetPosition() const override { return m_Light.GetProperties().position; }
    virtual void SetPosition(Vector3 position) override;
    virtual void Draw(const Camera& camera, bool selected) const override;
    virtual void Rotate(const Camera& camera, ImGuizmo::MODE axes) override;
    virtual void Scale(const Camera&) override {}

private:
    void DrawRange(const Camera& camera) const;

    LightComponent& m_Light;
    const ModelInstance& m_Model;
};

// 예전에 방위 -0.5 rad와 고도 0.75 x 90°로 정하던 기본 방향을 bit 단위로 옮겨서, 기본 화면이 그때와 같다.
const Vector3 kDefaultDirectionalLightDirection(0.335837096f, 0.923879147f, -0.18346864f);
constexpr float kDefaultLocalLightIntensity = 1.0f;
constexpr float kDefaultInnerConeAngle = 0.0f;
constexpr float kDefaultOuterConeAngle = XM_PIDIV4;
constexpr float kRangeWhenLimited = 1.0f;
constexpr float kSpotConeLengthInIconRadii = 8.0f;
constexpr int kWireframeCircleSegments = 48;
const ImU32 kLightColor = IM_COL32(255, 200, 0, 255);
const ImU32 kInnerConeColor = IM_COL32(255, 200, 0, 140);
const ImU32 kSelectedColor = IM_COL32(255, 128, 16, 255);

bool HasRotation(LightComponent::Type type)
{
    return type != LightComponent::Type::Point;
}

bool TakesRange(LightComponent::Type type)
{
    return type != LightComponent::Type::Directional;
}

bool IsSpotConeAngles(float innerConeAngle, float outerConeAngle)
{
    return innerConeAngle >= 0.0f && innerConeAngle < outerConeAngle && outerConeAngle <= XM_PIDIV2;
}

bool IsRange(float range)
{
    return range > 0.0f;
}

std::string FormatRange(const std::optional<float>& range)
{
    return range ? MeasurementConditions::FormatFloat(*range) : std::string();
}

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

std::vector<std::string> LightComponent::ListSettingKeys(Type type)
{
    switch (type)
    {
    case Type::Point: return { "Type", "Position", "Color", "Intensity", "Range" };
    case Type::Spot: return { "Type", "Direction", "Up", "Position", "Color", "Intensity", "Range", "Inner Cone Angle", "Outer Cone Angle" };
    default: return { "Type", "Direction", "Up", "Position", "Color", "Intensity" };
    }
}

LightComponent::Properties LightComponent::GetDefaultProperties(Type type)
{
    const bool directional = type == Type::Directional;
    Properties properties;
    properties.rotation = directional ? MakeRotationToward(kDefaultDirectionalLightDirection) : Matrix3(kIdentity);
    properties.position = Vector3(kZero);
    properties.color = Vector3(1.0f, 1.0f, 1.0f);
    properties.intensity = directional ? kDefaultDirectionalLightIntensity : kDefaultLocalLightIntensity;
    properties.innerConeAngle = kDefaultInnerConeAngle;
    properties.outerConeAngle = kDefaultOuterConeAngle;
    properties.range = std::nullopt;
    return properties;
}

LightComponent::LightComponent(Type type)
    : Component(ListSettingKeys(type)), m_Type(type), m_Properties(GetDefaultProperties(type))
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

const std::pair<LightComponent::Type, const char*> kLightTypeNames[] = {
    { LightComponent::Type::Directional, "Directional" }, { LightComponent::Type::Point, "Point" }, { LightComponent::Type::Spot, "Spot" } };

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

void LightComponent::SaveSettings(IniFile::Values& values) const
{
    values["Type"] = GetLightTypeName(m_Type);
    if (HasRotation(m_Type))
    {
        values["Direction"] = SettingValues::FormatVector(m_Properties.rotation.GetZ());
        values["Up"] = SettingValues::FormatVector(m_Properties.rotation.GetY());
    }
    values["Position"] = SettingValues::FormatVector(m_Properties.position);
    values["Color"] = SettingValues::FormatVector(m_Properties.color);
    char text[32];
    sprintf_s(text, "%.9g", m_Properties.intensity);
    values["Intensity"] = text;
    if (TakesRange(m_Type))
        values["Range"] = FormatRange(m_Properties.range);
    if (m_Type == Type::Spot)
    {
        values["Inner Cone Angle"] = MeasurementConditions::FormatFloat(m_Properties.innerConeAngle);
        values["Outer Cone Angle"] = MeasurementConditions::FormatFloat(m_Properties.outerConeAngle);
    }
}

void LightComponent::LoadSettings(const IniFile::Values& values)
{
    const auto read = [&values](const char* key, Vector3& vector)
    {
        const auto text = values.find(key);
        return text != values.end() && SettingValues::ReadVector(text->second, vector);
    };
    const auto readNumber = [&values](const char* key, float& number)
    {
        const auto text = values.find(key);
        float read = 0.0f;
        if (text == values.end() || !SettingValues::ReadNumbers(text->second, &read, 1))
            return false;
        number = read;
        return true;
    };

    Properties properties = GetDefaultProperties(m_Type);
    Vector3 direction, up, position, color;
    if (read("Direction", direction) && read("Up", up))
        MakeRotation(direction, up, properties.rotation);
    if (read("Position", position))
        properties.position = position;
    if (read("Color", color) && IsColor(color))
        properties.color = color;
    float intensity = 0.0f;
    if (readNumber("Intensity", intensity) && intensity >= 0.0f)
        properties.intensity = intensity;
    float range = 0.0f;
    if (readNumber("Range", range) && IsRange(range))
        properties.range = range;
    float innerConeAngle = properties.innerConeAngle;
    float outerConeAngle = properties.outerConeAngle;
    readNumber("Inner Cone Angle", innerConeAngle);
    readNumber("Outer Cone Angle", outerConeAngle);
    if (IsSpotConeAngles(innerConeAngle, outerConeAngle))
    {
        properties.innerConeAngle = innerConeAngle;
        properties.outerConeAngle = outerConeAngle;
    }
    m_Properties = properties;
}

void DescribeDirectionalLightConditions(const LightComponent& light, MeasurementConditions::Values& values)
{
    const LightComponent::Properties& properties = light.GetProperties();
    values["light direction"] = SettingValues::FormatVector(properties.rotation.GetZ());
    values["light up"] = SettingValues::FormatVector(properties.rotation.GetY());
    values["light color"] = SettingValues::FormatVector(properties.color);
    values["light intensity"] = MeasurementConditions::FormatFloat(properties.intensity);
}

void ReadRotationConditions(const MeasurementConditions::Values& values, const std::string& prefix, MeasurementConditions::Reading& reading, Matrix3& rotation)
{
    Vector3 direction, up;
    const bool hasDirection = MeasurementConditions::ReadValue(values, prefix + "direction", reading, [&direction](const std::string& text) { return SettingValues::ReadVector(text, direction); });
    const bool hasUp = MeasurementConditions::ReadValue(values, prefix + "up", reading, [&up](const std::string& text) { return SettingValues::ReadVector(text, up); });
    if (hasDirection && hasUp && !MakeRotation(direction, up, rotation))
    {
        reading.unread.push_back(prefix + "direction");
        reading.unread.push_back(prefix + "up");
    }
}

void ReadColorAndIntensityConditions(const MeasurementConditions::Values& values, const std::string& prefix, MeasurementConditions::Reading& reading,
    LightComponent::Properties& properties)
{
    MeasurementConditions::ReadValue(values, prefix + "color", reading, [&properties](const std::string& text) { return SettingValues::ReadVector(text, properties.color) && IsColor(properties.color); });
    MeasurementConditions::ReadValue(values, prefix + "intensity", reading, [&properties](const std::string& text)
    {
        return SettingValues::ReadNumbers(text, &properties.intensity, 1) && properties.intensity >= 0.0f;
    });
}

void ReadDirectionalLightConditions(LightComponent& light, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    LightComponent::Properties properties = light.GetProperties();
    ReadRotationConditions(values, "light ", reading, properties.rotation);
    ReadColorAndIntensityConditions(values, "light ", reading, properties);
    reading.applies.push_back([&light, properties] { light.SetProperties(properties); });
}

LightComponent* FindLocalLight(const Scene::Object& object)
{
    LightComponent* light = object.FindComponent<LightComponent>();
    return light != nullptr && light->GetType() != LightComponent::Type::Directional ? light : nullptr;
}

std::string GetConditionPrefix(const Scene::Object& object)
{
    std::string prefix = object.GetName() + " ";
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char letter) { return (char)tolower(letter); });
    return prefix;
}

void DescribeLocalLightConditions(MeasurementConditions::Values& values)
{
    for (const Scene::Object* object : Scene::GetObjects())
    {
        const LightComponent* light = FindLocalLight(*object);
        if (light == nullptr)
            continue;
        const std::string prefix = GetConditionPrefix(*object);
        const LightComponent::Properties& properties = light->GetProperties();
        values[prefix + "position"] = SettingValues::FormatVector(properties.position);
        values[prefix + "color"] = SettingValues::FormatVector(properties.color);
        values[prefix + "intensity"] = MeasurementConditions::FormatFloat(properties.intensity);
        if (properties.range)
            values[prefix + "range"] = FormatRange(properties.range);
        if (light->GetType() != LightComponent::Type::Spot)
            continue;
        values[prefix + "direction"] = SettingValues::FormatVector(properties.rotation.GetZ());
        values[prefix + "up"] = SettingValues::FormatVector(properties.rotation.GetY());
        values[prefix + "inner cone angle"] = MeasurementConditions::FormatFloat(properties.innerConeAngle);
        values[prefix + "outer cone angle"] = MeasurementConditions::FormatFloat(properties.outerConeAngle);
    }
}

void ReadLocalLightConditions(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
{
    for (Scene::Object* object : Scene::GetOptionalObjects())
    {
        LightComponent* light = FindLocalLight(*object);
        if (light == nullptr)
            continue;
        const std::string prefix = GetConditionPrefix(*object);
        const bool inLine = std::any_of(values.begin(), values.end(),
            [&prefix](const MeasurementConditions::Values::value_type& value) { return value.first.compare(0, prefix.size(), prefix) == 0; });
        if (!inLine)
        {
            reading.applies.push_back([object] { Scene::RemoveObject(*object); });
            continue;
        }

        LightComponent::Properties properties = light->GetProperties();
        MeasurementConditions::ReadValue(values, prefix + "position", reading, [&properties](const std::string& text) { return SettingValues::ReadVector(text, properties.position); });
        ReadColorAndIntensityConditions(values, prefix, reading, properties);
        properties.range = std::nullopt;
        if (values.count(prefix + "range") != 0)
        {
            MeasurementConditions::ReadValue(values, prefix + "range", reading, [&properties](const std::string& text)
            {
                float range = 0.0f;
                if (!SettingValues::ReadNumbers(text, &range, 1) || !IsRange(range))
                    return false;
                properties.range = range;
                return true;
            });
        }
        if (light->GetType() == LightComponent::Type::Spot)
        {
            ReadRotationConditions(values, prefix, reading, properties.rotation);
            const bool hasInner = MeasurementConditions::ReadValue(values, prefix + "inner cone angle", reading,
                [&properties](const std::string& text) { return SettingValues::ReadNumbers(text, &properties.innerConeAngle, 1); });
            const bool hasOuter = MeasurementConditions::ReadValue(values, prefix + "outer cone angle", reading,
                [&properties](const std::string& text) { return SettingValues::ReadNumbers(text, &properties.outerConeAngle, 1); });
            if (hasInner && hasOuter && !IsSpotConeAngles(properties.innerConeAngle, properties.outerConeAngle))
            {
                reading.unread.push_back(prefix + "inner cone angle");
                reading.unread.push_back(prefix + "outer cone angle");
            }
        }
        reading.applies.push_back([object, light, properties]
        {
            if (!Scene::HasObject(*object))
            {
                Scene::AddObject(*object);
                object->LoadSettings(IniFile::Values());
            }
            light->SetProperties(properties);
        });
    }
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

ControlChannel::Fields DescribeRotation(const Matrix3& matrix)
{
    const Quaternion rotation = GetCanonicalQuaternion(Quaternion((XMMATRIX)matrix));
    return ControlChannel::Fields()
        .Float("x", XMVectorGetX(rotation)).Float("y", XMVectorGetY(rotation)).Float("z", XMVectorGetZ(rotation)).Float("w", XMVectorGetW(rotation));
}

ControlChannel::Fields DescribeColor(Vector3 color)
{
    return ControlChannel::Fields().Float("r", color.GetX()).Float("g", color.GetY()).Float("b", color.GetZ());
}

ControlChannel::Fields DescribeDirectionalLight(const LightComponent& light)
{
    const LightComponent::Properties& properties = light.GetProperties();
    const Vector3 direction = light.GetDirection();
    return ControlChannel::Fields()
        .Object("rotation", DescribeRotation(properties.rotation))
        .Object("direction", ControlChannel::DescribeVector(direction))
        .Float("elevation", GetElevation(direction))
        .Float("azimuth", GetAzimuth(direction))
        .Object("color", DescribeColor(properties.color))
        .Float("intensity", properties.intensity)
        .Object("position", ControlChannel::DescribeVector(properties.position));
}

ControlChannel::Fields DescribeLocalLight(const LightComponent& light)
{
    const LightComponent::Properties& properties = light.GetProperties();
    ControlChannel::Fields fields;
    fields.Text("type", GetLightTypeName(light.GetType()))
        .Object("position", ControlChannel::DescribeVector(properties.position));
    if (light.GetType() == LightComponent::Type::Spot)
    {
        fields.Object("rotation", DescribeRotation(properties.rotation))
            .Object("direction", ControlChannel::DescribeVector(light.GetDirection()))
            .Float("inner_cone_angle", properties.innerConeAngle)
            .Float("outer_cone_angle", properties.outerConeAngle);
    }
    fields.Object("color", DescribeColor(properties.color)).Float("intensity", properties.intensity).Flag("limit_range", properties.range.has_value());
    if (properties.range)
        fields.Float("range", *properties.range);
    return fields;
}

ControlChannel::Fields DescribeLocalLights(void)
{
    ControlChannel::Fields lights;
    for (const Scene::Object* object : Scene::GetObjects())
    {
        if (const LightComponent* light = FindLocalLight(*object))
            lights.Object(object->GetName(), DescribeLocalLight(*light));
    }
    return lights;
}

// 받은 값과 다시 읽은 고도와 방위가 같다고 보는 차이. degree와 radian을 오가며 생기는 float 오차보다 크게 잡았다.
constexpr float kLightDegreesTolerance = 1e-4f;

void SetDirectionalLight(LightComponent& light, const std::string& arguments, const ControlChannel::Reply& reply)
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

void RegisterDirectionalLight(LightComponent& light)
{
    ControlChannel::RegisterState("light", [&light] { return DescribeDirectionalLight(light); });
    ControlChannel::Register("light", "light set <key>=<value> ... | light reset: the directional light, as its rotate and translate gizmos and its "
        "Details panel change it. reset sets the transform to no rotation at the origin, so the light goes along world -Z, "
        "and keeps the color and intensity, as Reset Transform in the Details panel does. It is a KHR_lights_punctual directional light and goes along the local -Z of its transform. "
        "Its Type, which editor get gives and the scene file keeps, is Directional, and light set does not change it. "
        "The Point Light and Spot Light, which editor add puts in the scene, are the other types, and editor set changes them. "
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
        SetDirectionalLight(light, arguments.substr(4), reply);
    });
}

void RegisterLocalLights(void)
{
    ControlChannel::RegisterState("local_lights", DescribeLocalLights);
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

void DrawSunIcon(ImDrawList& drawList, ImVec2 icon, float radius, ImU32 color, float thickness)
{
    drawList.AddCircleFilled(icon, radius * 0.45f, color);
    const int kRayCount = 8;
    for (int i = 0; i < kRayCount; ++i)
    {
        const float angle = XM_2PI * i / kRayCount;
        const ImVec2 direction(cosf(angle), sinf(angle));
        drawList.AddLine(
            ImVec2(icon.x + direction.x * radius * 0.65f, icon.y + direction.y * radius * 0.65f),
            ImVec2(icon.x + direction.x * radius, icon.y + direction.y * radius),
            color, thickness);
    }
}

void DrawBulbIcon(ImDrawList& drawList, ImVec2 icon, float radius, ImU32 color)
{
    drawList.AddCircleFilled(ImVec2(icon.x, icon.y - radius * 0.2f), radius * 0.55f, color);
    drawList.AddRectFilled(ImVec2(icon.x - radius * 0.28f, icon.y + radius * 0.4f), ImVec2(icon.x + radius * 0.28f, icon.y + radius * 0.56f), color);
    drawList.AddRectFilled(ImVec2(icon.x - radius * 0.2f, icon.y + radius * 0.64f), ImVec2(icon.x + radius * 0.2f, icon.y + radius * 0.8f), color);
}

void DrawSpotIcon(ImDrawList& drawList, ImVec2 icon, float radius, ImU32 color, float thickness)
{
    drawList.AddCircle(icon, radius * 0.75f, color, 0, thickness);
    drawList.AddCircleFilled(icon, radius * 0.35f, color);
}

void DrawWorldLine(ImDrawList& drawList, const Camera& camera, Vector3 from, Vector3 to, ImU32 color, float thickness)
{
    ImVec2 screenFrom, screenTo;
    if (SceneEditor::ProjectSegmentToScene(camera, from, to, screenFrom, screenTo))
        drawList.AddLine(screenFrom, screenTo, color, thickness);
}

void DrawSpotCone(ImDrawList& drawList, const Camera& camera, const LightComponent& light, float slantLength, ImU32 outerColor, ImU32 innerColor,
    float thickness)
{
    const LightComponent::Properties& properties = light.GetProperties();
    const Vector3 apex = properties.position;
    const Vector3 emission = -properties.rotation.GetZ();
    const Vector3 x = properties.rotation.GetX();
    const Vector3 y = properties.rotation.GetY();
    const auto pointOnCone = [&](float coneAngle, float around)
    {
        return apex + (emission * cosf(coneAngle) + (x * cosf(around) + y * sinf(around)) * sinf(coneAngle)) * slantLength;
    };

    for (const auto& cone : { std::make_pair(properties.outerConeAngle, outerColor), std::make_pair(properties.innerConeAngle, innerColor) })
    {
        for (int i = 0; i < kWireframeCircleSegments; ++i)
        {
            const float start = XM_2PI * i / kWireframeCircleSegments;
            const float end = XM_2PI * (i + 1) / kWireframeCircleSegments;
            DrawWorldLine(drawList, camera, pointOnCone(cone.first, start), pointOnCone(cone.first, end), cone.second, thickness);
        }
    }
    const int kSideLineCount = 4;
    for (int i = 0; i < kSideLineCount; ++i)
        DrawWorldLine(drawList, camera, apex, pointOnCone(properties.outerConeAngle, XM_2PI * i / kSideLineCount), outerColor, thickness);
}

void DrawSphere(ImDrawList& drawList, const Camera& camera, Vector3 center, float radius, ImU32 color, float thickness)
{
    const Vector3 axes[] = { Vector3(kXUnitVector), Vector3(kYUnitVector), Vector3(kZUnitVector) };
    for (int plane = 0; plane < 3; ++plane)
    {
        const Vector3 x = axes[plane];
        const Vector3 y = axes[(plane + 1) % 3];
        const auto pointOnCircle = [&](float around) { return center + (x * cosf(around) + y * sinf(around)) * radius; };
        for (int i = 0; i < kWireframeCircleSegments; ++i)
        {
            const float start = XM_2PI * i / kWireframeCircleSegments;
            const float end = XM_2PI * (i + 1) / kWireframeCircleSegments;
            DrawWorldLine(drawList, camera, pointOnCircle(start), pointOnCircle(end), color, thickness);
        }
    }
}

float GetIconThickness(void)
{
    return SceneEditor::GetIconRadius() * 0.15f;
}

float GetWireframeThickness(void)
{
    return GetIconThickness() * 0.6f;
}

void LightIconComponent::SetPosition(Vector3 position)
{
    LightComponent::Properties properties = m_Light.GetProperties();
    properties.position = position;
    m_Light.SetProperties(properties);
}

void LightIconComponent::DrawRange(const Camera& camera) const
{
    const LightComponent::Properties& properties = m_Light.GetProperties();
    const float worldRange = *properties.range * m_Model.GetScale();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (m_Light.GetType() == LightComponent::Type::Spot)
        DrawSpotCone(*drawList, camera, m_Light, worldRange, kLightColor, kInnerConeColor, GetWireframeThickness());
    else
        DrawSphere(*drawList, camera, properties.position, worldRange, kLightColor, GetWireframeThickness());
}

void LightIconComponent::Draw(const Camera& camera, bool selected) const
{
    const bool hasRange = m_Light.GetProperties().range.has_value();
    if (selected && hasRange)
        DrawRange(camera);

    const Vector3 position = GetPosition();
    ImVec2 icon;
    if (!SceneEditor::ProjectToScene(camera, position, icon))
        return;

    const float radius = SceneEditor::GetIconRadius();
    const float thickness = GetIconThickness();
    const float worldLengthPerPixel = SceneEditor::GetWorldLengthPerPixel(camera, position);
    const LightComponent::Type type = m_Light.GetType();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (type == LightComponent::Type::Spot && selected && !hasRange)
        DrawSpotCone(*drawList, camera, m_Light, radius * kSpotConeLengthInIconRadii * worldLengthPerPixel, kLightColor, kInnerConeColor, GetWireframeThickness());

    if (HasRotation(type))
    {
        const float arrowLength = radius * 4.0f * worldLengthPerPixel;
        ImVec2 arrowTip;
        if (SceneEditor::ProjectToScene(camera, position - m_Light.GetDirection() * arrowLength, arrowTip))
            DrawArrow(*drawList, icon, arrowTip, kLightColor, thickness);
    }

    switch (type)
    {
    case LightComponent::Type::Point: DrawBulbIcon(*drawList, icon, radius, kLightColor); break;
    case LightComponent::Type::Spot: DrawSpotIcon(*drawList, icon, radius, kLightColor, thickness); break;
    default: DrawSunIcon(*drawList, icon, radius, kLightColor, thickness); break;
    }

    if (selected)
        drawList->AddCircle(icon, radius * 1.2f, kSelectedColor, 0, thickness);
}

void LightIconComponent::Rotate(const Camera& camera, ImGuizmo::MODE axes)
{
    if (!HasRotation(m_Light.GetType()))
        return;
    LightComponent::Properties properties = m_Light.GetProperties();
    Matrix4 transform(properties.rotation, properties.position);
    if (!SceneEditor::Manipulate(camera, ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z, axes, transform))
        return;
    // gizmo가 돌린 matrix는 float 오차로 직교에서 조금씩 벗어나므로 quaternion을 거쳐 직교 matrix로 되돌린다.
    properties.rotation = Matrix3(Normalize(Quaternion((XMMATRIX)transform.Get3x3())));
    m_Light.SetProperties(properties);
}

void ShowIntensity(LightComponent::Type type, float& intensity)
{
    if (type == LightComponent::Type::Directional)
    {
        if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, FLT_MAX, "%.3f lux"))
            intensity = std::max(0.0f, intensity);
        ImGui::SetItemTooltip("Illuminance on a surface facing the light, in lux.");
        return;
    }
    if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, FLT_MAX, "%.3f cd"))
        intensity = std::max(0.0f, intensity);
    ImGui::SetItemTooltip(type == LightComponent::Type::Spot
        ? "Luminous intensity inside the inner cone, in candela."
        : "Luminous intensity in every direction, in candela.");
}

void ShowRange(std::optional<float>& range)
{
    bool limitRange = range.has_value();
    if (ImGui::Checkbox("Limit Range", &limitRange))
        range = limitRange ? std::optional<float>(kRangeWhenLimited) : std::nullopt;
    ImGui::SetItemTooltip("Off, the light reaches infinitely far, as a KHR_lights_punctual light without range does. "
        "The scene file keeps a Range only while this is on.");
    if (!range)
        return;
    float limited = *range;
    if (ImGui::DragFloat("Range", &limited, 0.01f, 0.001f, FLT_MAX, "%.3f m", ImGuiSliderFlags_AlwaysClamp) && IsRange(limited))
        range = limited;
    ImGui::SetItemTooltip("Distance from the light, in metres, beyond which it lights nothing. It is in the metres of the glTF model, "
        "while Position is in world units, which are those metres times the scale the scene file gives the model. "
        "While the light is selected, the scene shows the range.");
}

void ShowConeAngles(LightComponent::Properties& properties)
{
    float innerConeAngle = properties.innerConeAngle;
    float outerConeAngle = properties.outerConeAngle;
    bool changed = ImGui::SliderAngle("Inner Cone Angle", &innerConeAngle, 0.0f, XMConvertToDegrees(outerConeAngle), "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Angle from the center of the cone where the falloff begins, 0 or more and below the outer cone angle. "
        "The scene file keeps it in radians.");
    changed |= ImGui::SliderAngle("Outer Cone Angle", &outerConeAngle, XMConvertToDegrees(innerConeAngle), 90.0f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Angle from the center of the cone where the falloff ends, above the inner cone angle and up to 90 degrees. "
        "The scene file keeps it in radians.");
    if (changed && IsSpotConeAngles(innerConeAngle, outerConeAngle))
    {
        properties.innerConeAngle = innerConeAngle;
        properties.outerConeAngle = outerConeAngle;
    }
}

void ShowLightDetails(LightComponent& light)
{
    const LightComponent::Type type = light.GetType();
    ImGui::LabelText("Type", "%s", GetLightTypeName(type));
    ImGui::SetItemTooltip("KHR_lights_punctual type of the light, which the scene file keeps as Type. Each light object keeps its type. "
        "The renderer draws only the directional light for now.");

    LightComponent::Properties properties = light.GetProperties();
    float color[3] = { properties.color.GetX(), properties.color.GetY(), properties.color.GetZ() };
    if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_Float))
        properties.color = Min(Max(Vector3(color[0], color[1], color[2]), Vector3(kZero)), Vector3(1.0f, 1.0f, 1.0f));
    ImGui::SetItemTooltip("Linear RGB, each 0 to 1.");
    ShowIntensity(type, properties.intensity);
    if (TakesRange(type))
        ShowRange(properties.range);
    if (type == LightComponent::Type::Spot)
        ShowConeAngles(properties);

    ImGui::SeparatorText("Transform");
    if (type != LightComponent::Type::Directional)
    {
        float position[3] = { properties.position.GetX(), properties.position.GetY(), properties.position.GetZ() };
        if (ImGui::DragFloat3("Position", position, 1.0f))
            properties.position = Vector3(position[0], position[1], position[2]);
        ImGui::SetItemTooltip("Position of the light in world units. The translate gizmo (W) moves it too.");
    }
    light.SetProperties(properties);

    if (!HasRotation(type))
    {
        ImGui::TextDisabled("A point light shines the same in every direction, so it does not rotate.");
        return;
    }
    const Vector3 direction = light.GetDirection();
    ImGui::Text("Toward the light: %.3f, %.3f, %.3f", (float)direction.GetX(), (float)direction.GetY(), (float)direction.GetZ());
    ImGui::Text("Elevation %.2f, azimuth %.2f degrees", GetElevation(direction), GetAzimuth(direction));
    if (ImGui::Button("Reset Transform"))
        light.ResetTransform();
    ImGui::SetItemTooltip(type == LightComponent::Type::Spot
        ? "Sets the transform to no rotation at the origin, so the light shines along world -Z. The color, intensity and cone angles stay."
        : "Sets the transform to no rotation at the origin, so the light goes along world -Z. The color and intensity stay.");
    ImGui::TextDisabled("Rotate the light with the gizmo (E). The toolbar on the scene picks local or world rings.");
}

LightComponent& AssembleLight(Scene::Object& object, LightComponent::Type type, const ModelInstance& model)
{
    LightComponent& light = object.AddComponent<LightComponent>(type);
    object.AddComponent<LightIconComponent>(light, model);
    return light;
}
