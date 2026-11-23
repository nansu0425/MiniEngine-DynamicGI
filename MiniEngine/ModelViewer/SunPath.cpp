#include "Core/pch.h"
#include "ModelViewer/SunPath.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Scene/SettingValues.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

using namespace Math;

namespace
{
    constexpr float kMaxMinElevation = 85.0f;
    constexpr float kMaxFrameSeconds = 0.1f;
    constexpr int kMaxAdvanceSteps = 8;
    constexpr float kEndSnapDegrees = 1e-3f;
    const char* const kEasingNames[] = { "Linear", "Ease In Out" };
    const char kConditionsGroup[] = "sun path";

    struct Arc
    {
        Vector3 axis;
        Vector3 horizon;
        float minAngle;
        float maxAngle;
    };

    Arc GetArc(const SunPathComponent::Settings& settings)
    {
        const float azimuth = XMConvertToRadians(settings.axisAzimuth);
        const Vector3 axis(cosf(azimuth), 0.0f, sinf(azimuth));
        return { axis, Cross(axis, Vector3(kYUnitVector)), settings.minElevation, 180.0f - settings.minElevation };
    }

    float FindArcAngle(const Arc& arc, Vector3 direction)
    {
        const float angle = XMConvertToDegrees(atan2f((float)direction.GetY(), (float)Dot(direction, arc.horizon)));
        if (angle < -90.0f)
            return arc.maxAngle;
        return std::clamp(angle, arc.minAngle, arc.maxAngle);
    }

    Matrix3 MakeArcRotation(const Arc& arc, float angle)
    {
        const float radians = XMConvertToRadians(angle);
        const Vector3 direction = arc.horizon * Scalar(cosf(radians)) + Vector3(kYUnitVector) * Scalar(sinf(radians));
        return Matrix3(arc.axis, Cross(direction, arc.axis), direction);
    }

    float Ease(SunPathComponent::Easing easing, float leg)
    {
        if (easing == SunPathComponent::Easing::Linear)
            return leg;
        return (1.0f - cosf(XM_PI * leg)) * 0.5f;
    }

    float FindLeg(SunPathComponent::Easing easing, float eased)
    {
        if (easing == SunPathComponent::Easing::Linear)
            return eased;
        return std::clamp(acosf(std::clamp(1.0f - 2.0f * eased, -1.0f, 1.0f)) / XM_PI, 0.0f, 1.0f);
    }

    const char* GetEasingName(SunPathComponent::Easing easing)
    {
        return kEasingNames[(int)easing];
    }

    bool ReadEasing(const std::string& text, SunPathComponent::Easing& easing)
    {
        const auto name = std::find(std::begin(kEasingNames), std::end(kEasingNames), text);
        if (name == std::end(kEasingNames))
            return false;
        easing = (SunPathComponent::Easing)(name - std::begin(kEasingNames));
        return true;
    }

    bool IsHoldSeconds(float value)
    {
        return std::isfinite(value) && value >= 0.0f;
    }

    bool IsMinElevation(float value)
    {
        return std::isfinite(value) && value >= 0.0f && value <= kMaxMinElevation;
    }

    bool IsDegreesPerSecond(float value)
    {
        return std::isfinite(value) && value >= 0.0f;
    }

    ControlChannel::Fields DescribeSunPath(const SunPathComponent& sunPath)
    {
        const SunPathComponent::Settings& settings = sunPath.GetSettings();
        return ControlChannel::Fields()
            .Flag("enabled", settings.enabled)
            .Float("axis_azimuth", settings.axisAzimuth)
            .Float("min_elevation", settings.minElevation)
            .Float("degrees_per_second", settings.degreesPerSecond)
            .Flag("ping_pong", settings.pingPong)
            .Text("easing", GetEasingName(settings.easing))
            .Float("hold_seconds", settings.holdSeconds)
            .Float("angle", sunPath.GetArcAngle())
            .Integer("travel", sunPath.GetTravel())
            .Float("hold_elapsed", sunPath.GetHoldElapsed());
    }

    void HandleSunPathCommand(SunPathComponent& sunPath, const std::string& arguments, const ControlChannel::Reply& reply)
    {
        const std::string usage = "usage: sun-path advance <seconds>";
        if (arguments.compare(0, 8, "advance ") != 0)
        {
            reply.Error(usage);
            return;
        }
        float seconds = 0.0f;
        if (!MeasurementConditions::ReadFloat(arguments.substr(8), seconds) || !std::isfinite(seconds) || seconds < 0.0f)
        {
            reply.Error("seconds is a finite number of 0 or more. " + usage);
            return;
        }
        sunPath.Advance(seconds);
        reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("sun_path")));
    }

    std::string FormatSwitch(bool value)
    {
        return value ? "on" : "off";
    }

    bool ReadSwitch(const std::string& text, bool& value)
    {
        if (text != "on" && text != "off")
            return false;
        value = text == "on";
        return true;
    }

    void DescribeSunPathConditions(const SunPathComponent& sunPath, MeasurementConditions::Values& values)
    {
        const SunPathComponent::Settings& settings = sunPath.GetSettings();
        values["sun path"] = FormatSwitch(settings.enabled);
        values["sun path axis azimuth"] = MeasurementConditions::FormatFloat(settings.axisAzimuth);
        values["sun path min elevation"] = MeasurementConditions::FormatFloat(settings.minElevation);
        values["sun path degrees per second"] = MeasurementConditions::FormatFloat(settings.degreesPerSecond);
        values["sun path ping pong"] = FormatSwitch(settings.pingPong);
        values["sun path easing"] = GetEasingName(settings.easing);
        values["sun path hold seconds"] = MeasurementConditions::FormatFloat(settings.holdSeconds);
        values["sun path travel"] = std::to_string(sunPath.GetTravel());
        values["sun path hold elapsed"] = MeasurementConditions::FormatFloat(sunPath.GetHoldElapsed());
    }

    void ReadSunPathConditions(SunPathComponent& sunPath, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
    {
        SunPathComponent::Settings settings = sunPath.GetSettings();
        if (values.find("sun path") == values.end())
        {
            settings.enabled = false;
            reading.applies.push_back([&sunPath, settings] { sunPath.SetSettings(settings); });
            return;
        }

        int travel = sunPath.GetTravel();
        MeasurementConditions::ReadValue(values, "sun path", reading, [&settings](const std::string& text) { return ReadSwitch(text, settings.enabled); });
        MeasurementConditions::ReadValue(values, "sun path axis azimuth", reading, [&settings](const std::string& text)
        {
            return MeasurementConditions::ReadFloat(text, settings.axisAzimuth) && std::isfinite(settings.axisAzimuth);
        });
        MeasurementConditions::ReadValue(values, "sun path min elevation", reading, [&settings](const std::string& text)
        {
            return MeasurementConditions::ReadFloat(text, settings.minElevation) && IsMinElevation(settings.minElevation);
        });
        MeasurementConditions::ReadValue(values, "sun path degrees per second", reading, [&settings](const std::string& text)
        {
            return MeasurementConditions::ReadFloat(text, settings.degreesPerSecond) && IsDegreesPerSecond(settings.degreesPerSecond);
        });
        MeasurementConditions::ReadValue(values, "sun path ping pong", reading, [&settings](const std::string& text) { return ReadSwitch(text, settings.pingPong); });
        MeasurementConditions::ReadValue(values, "sun path travel", reading, [&travel](const std::string& text)
        {
            if (text != "1" && text != "-1")
                return false;
            travel = text == "1" ? 1 : -1;
            return true;
        });
        settings.easing = SunPathComponent::Easing::Linear;
        settings.holdSeconds = 0.0f;
        float holdElapsed = 0.0f;
        if (values.find("sun path easing") != values.end())
        {
            MeasurementConditions::ReadValue(values, "sun path easing", reading, [&settings](const std::string& text) { return ReadEasing(text, settings.easing); });
            MeasurementConditions::ReadValue(values, "sun path hold seconds", reading, [&settings](const std::string& text)
            {
                return MeasurementConditions::ReadFloat(text, settings.holdSeconds) && IsHoldSeconds(settings.holdSeconds);
            });
            MeasurementConditions::ReadValue(values, "sun path hold elapsed", reading, [&holdElapsed](const std::string& text)
            {
                return MeasurementConditions::ReadFloat(text, holdElapsed) && IsHoldSeconds(holdElapsed);
            });
        }
        reading.applies.push_back([&sunPath, settings, travel, holdElapsed]
        {
            sunPath.SetSettings(settings);
            sunPath.RestoreProgress(travel, holdElapsed);
        });
    }
}

SunPathComponent::Settings SunPathComponent::GetDefaultSettings()
{
    return { false, 0.0f, 10.0f, 3.0f, true, Easing::Linear, 0.0f };
}

SunPathComponent::SunPathComponent(LightComponent& light)
    : Component({ "Sun Path", "Sun Path Axis Azimuth", "Sun Path Min Elevation", "Sun Path Degrees per Second", "Sun Path Ping Pong",
        "Sun Path Easing", "Sun Path Hold Seconds" }),
    m_Light(light), m_Settings(GetDefaultSettings())
{
}

float SunPathComponent::GetArcAngle() const
{
    return FindArcAngle(GetArc(m_Settings), m_Light.GetDirection());
}

void SunPathComponent::RestoreProgress(int travel, float holdElapsed)
{
    m_Progress.travel = travel < 0 ? -1 : 1;
    m_Progress.holdElapsed = holdElapsed;
    m_Written.valid = false;
}

void SunPathComponent::FollowLight()
{
    const Vector3 direction = m_Light.GetDirection();
    if (m_Written.valid && XMVector3Equal(direction, m_Written.direction) && m_Written.axisAzimuth == m_Settings.axisAzimuth
        && m_Written.minElevation == m_Settings.minElevation && m_Written.easing == m_Settings.easing)
    {
        return;
    }
    const Arc arc = GetArc(m_Settings);
    const float angle = FindArcAngle(arc, direction);
    const float legEnd = m_Progress.travel > 0 ? arc.maxAngle : arc.minAngle;
    const float legStart = m_Progress.travel > 0 ? arc.minAngle : arc.maxAngle;
    if (fabsf(angle - legEnd) < kEndSnapDegrees)
        m_Progress.leg = 1.0f;
    else if (fabsf(angle - legStart) < kEndSnapDegrees)
        m_Progress.leg = 0.0f;
    else
        m_Progress.leg = FindLeg(m_Settings.easing, fabsf(angle - legStart) / (arc.maxAngle - arc.minAngle));
    if (m_Progress.leg < 1.0f)
        m_Progress.holdElapsed = 0.0f;
}

void SunPathComponent::WriteLight()
{
    const Arc arc = GetArc(m_Settings);
    const float span = arc.maxAngle - arc.minAngle;
    const float eased = Ease(m_Settings.easing, m_Progress.leg);
    const float angle = m_Progress.travel > 0 ? arc.minAngle + span * eased : arc.maxAngle - span * eased;
    LightComponent::Properties properties = m_Light.GetProperties();
    properties.rotation = MakeArcRotation(arc, angle);
    m_Light.SetProperties(properties);
    m_Written = { true, properties.rotation.GetZ(), m_Settings.axisAzimuth, m_Settings.minElevation, m_Settings.easing };
}

void SunPathComponent::Advance(float seconds)
{
    FollowLight();
    const float span = 180.0f - 2.0f * m_Settings.minElevation;
    const float legSeconds = m_Settings.degreesPerSecond > 0.0f ? span / m_Settings.degreesPerSecond : INFINITY;
    float remaining = seconds;
    if (std::isfinite(legSeconds))
    {
        const float period = (legSeconds + m_Settings.holdSeconds) * (m_Settings.pingPong ? 2.0f : 1.0f);
        if (remaining > period)
            remaining = fmodf(remaining, period);
    }

    for (int step = 0; step < kMaxAdvanceSteps && remaining > 0.0f; ++step)
    {
        if (m_Progress.leg < 1.0f)
        {
            if (!std::isfinite(legSeconds))
                break;
            const float needed = (1.0f - m_Progress.leg) * legSeconds;
            if (remaining < needed)
            {
                m_Progress.leg = std::min(m_Progress.leg + remaining / legSeconds, 1.0f);
                break;
            }
            remaining -= needed;
            m_Progress.leg = 1.0f;
            m_Progress.holdElapsed = 0.0f;
        }
        const float holdLeft = std::max(m_Settings.holdSeconds - m_Progress.holdElapsed, 0.0f);
        if (remaining < holdLeft)
        {
            m_Progress.holdElapsed += remaining;
            break;
        }
        remaining -= holdLeft;
        if (m_Settings.pingPong)
            m_Progress.travel = -m_Progress.travel;
        m_Progress.leg = 0.0f;
        m_Progress.holdElapsed = 0.0f;
    }
    WriteLight();
}

void SunPathComponent::Update(float frameSeconds)
{
    if (m_Settings.enabled)
        Advance(std::min(frameSeconds, kMaxFrameSeconds));
}

void SunPathComponent::SaveSettings(IniFile::Values& values) const
{
    values["Sun Path"] = SettingValues::FormatFlag(m_Settings.enabled);
    values["Sun Path Axis Azimuth"] = MeasurementConditions::FormatFloat(m_Settings.axisAzimuth);
    values["Sun Path Min Elevation"] = MeasurementConditions::FormatFloat(m_Settings.minElevation);
    values["Sun Path Degrees per Second"] = MeasurementConditions::FormatFloat(m_Settings.degreesPerSecond);
    values["Sun Path Ping Pong"] = SettingValues::FormatFlag(m_Settings.pingPong);
    values["Sun Path Easing"] = GetEasingName(m_Settings.easing);
    values["Sun Path Hold Seconds"] = MeasurementConditions::FormatFloat(m_Settings.holdSeconds);
}

void SunPathComponent::LoadSettings(const IniFile::Values& values)
{
    const Settings defaults = GetDefaultSettings();
    Settings settings = defaults;
    SettingValues::ReadFlag(values, "Sun Path", settings.enabled);
    SettingValues::ReadFloat(values, "Sun Path Axis Azimuth", settings.axisAzimuth);
    SettingValues::ReadFloat(values, "Sun Path Min Elevation", settings.minElevation);
    SettingValues::ReadFloat(values, "Sun Path Degrees per Second", settings.degreesPerSecond);
    SettingValues::ReadFlag(values, "Sun Path Ping Pong", settings.pingPong);
    SettingValues::ReadName(values, "Sun Path Easing", kEasingNames, settings.easing);
    SettingValues::ReadFloat(values, "Sun Path Hold Seconds", settings.holdSeconds);
    if (!std::isfinite(settings.axisAzimuth))
        settings.axisAzimuth = defaults.axisAzimuth;
    if (!IsMinElevation(settings.minElevation))
        settings.minElevation = defaults.minElevation;
    if (!IsDegreesPerSecond(settings.degreesPerSecond))
        settings.degreesPerSecond = defaults.degreesPerSecond;
    if (!IsHoldSeconds(settings.holdSeconds))
        settings.holdSeconds = defaults.holdSeconds;
    m_Settings = settings;
}

void RegisterSunPath(SunPathComponent& sunPath)
{
    ControlChannel::RegisterState("sun_path", [&sunPath] { return DescribeSunPath(sunPath); });
    ControlChannel::Register("sun-path", "sun-path advance <seconds>: moves the directional light along the sun path for the given seconds, "
        "0 or more, as each frame does with the measured frame time while the path is enabled, but also while it is disabled. "
        "The path is the arc of directions that turn about a horizontal axis from Min Elevation above the horizon through the zenith "
        "to Min Elevation above the opposite horizon. angle in state sun_path is the place on that arc in degrees, from Min Elevation to "
        "180 - Min Elevation, found from the light direction. travel is 1 toward 180 - Min Elevation and -1 back. "
        "A leg from one end to the other takes the span divided by Degrees per Second, whatever the Easing. Linear keeps the speed, "
        "and Ease In Out slows to 0 at both ends along a cosine. At the end of a leg the light holds for Hold Seconds, and hold_elapsed "
        "is how long it has held. Then with Ping Pong it turns back and travel flips, and without it the light jumps to the start of the leg. "
        "When the light was turned elsewhere, as by light set, the gizmo or restore, or the path settings change, the path continues "
        "from the place of the light direction on the arc, a direction off the arc taking the nearest place. "
        "A frame advances by at most 0.1 seconds. The settings are those of the Directional Light object, which editor get and editor set read and change: "
        "Sun Path, Sun Path Axis Azimuth in degrees as light set azimuth, Sun Path Min Elevation from 0 to 85, "
        "Sun Path Degrees per Second of 0 or more, Sun Path Ping Pong, Sun Path Easing (Linear or Ease In Out) and Sun Path Hold Seconds of 0 or more. "
        "It replies with state sun_path",
        [&sunPath](const std::string& arguments, const ControlChannel::Reply& reply) { HandleSunPathCommand(sunPath, arguments, reply); });
    MeasurementConditions::Register(kConditionsGroup, {
        [&sunPath](MeasurementConditions::Values& values) { DescribeSunPathConditions(sunPath, values); },
        [&sunPath](const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading) { ReadSunPathConditions(sunPath, values, reading); },
        { "sun_path" } });
}

void UnregisterSunPath(void)
{
    MeasurementConditions::Unregister(kConditionsGroup);
}

void ShowSunPathDetails(SunPathComponent& sunPath)
{
    ImGui::SeparatorText("Sun Path");
    SunPathComponent::Settings settings = sunPath.GetSettings();
    ImGui::Checkbox("Enabled", &settings.enabled);
    ImGui::SetItemTooltip("Moves the light along the path in real time, whatever the frame rate.");
    ImGui::SliderFloat("Axis Azimuth", &settings.axisAzimuth, 0.0f, 360.0f, "%.1f deg");
    ImGui::SetItemTooltip("Azimuth of the horizontal axis the path turns about. 0 is world +X, and the light then crosses the sky over +Z, the zenith and -Z.");
    if (ImGui::SliderFloat("Min Elevation", &settings.minElevation, 0.0f, kMaxMinElevation, "%.1f deg"))
        settings.minElevation = std::clamp(settings.minElevation, 0.0f, kMaxMinElevation);
    ImGui::SetItemTooltip("Elevation of both ends of the path above the horizon.");
    if (ImGui::DragFloat("Degrees per Second", &settings.degreesPerSecond, 0.1f, 0.0f, FLT_MAX, "%.2f"))
        settings.degreesPerSecond = std::max(0.0f, settings.degreesPerSecond);
    ImGui::SetItemTooltip("Mean speed along the path. A leg from one end to the other takes the same time whatever the Easing.");
    ImGui::Checkbox("Ping Pong", &settings.pingPong);
    ImGui::SetItemTooltip("Turns back at either end. Off jumps from the end to the other end.");
    int easing = (int)settings.easing;
    if (ImGui::Combo("Easing", &easing, kEasingNames, IM_ARRAYSIZE(kEasingNames)))
        settings.easing = (SunPathComponent::Easing)easing;
    ImGui::SetItemTooltip("Linear keeps the speed. Ease In Out slows to 0 at both ends along a cosine and is fastest midway, at pi/2 times the mean speed.");
    if (ImGui::DragFloat("Hold Seconds", &settings.holdSeconds, 0.1f, 0.0f, FLT_MAX, "%.2f s"))
        settings.holdSeconds = std::max(0.0f, settings.holdSeconds);
    ImGui::SetItemTooltip("How long the light stays at an end before it turns back or jumps.");
    sunPath.SetSettings(settings);
    ImGui::Text("Angle %.2f deg, travel %+d, held %.2f s", sunPath.GetArcAngle(), sunPath.GetTravel(), sunPath.GetHoldElapsed());
    ImGui::SetItemTooltip("Place of the light on the path, from Min Elevation to 180 - Min Elevation.");
}
