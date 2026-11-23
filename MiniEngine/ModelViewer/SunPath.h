#pragma once

#include "Core/Scene/Scene.h"
#include "ModelViewer/Light.h"

class SunPathComponent : public Scene::Component
{
public:
    enum class Easing { Linear, EaseInOut };

    struct Settings
    {
        bool enabled;
        float axisAzimuth;
        float minElevation;
        float degreesPerSecond;
        bool pingPong;
        Easing easing;
        float holdSeconds;
    };

    explicit SunPathComponent(LightComponent& light);

    const Settings& GetSettings() const { return m_Settings; }
    void SetSettings(const Settings& settings) { m_Settings = settings; }
    int GetTravel() const { return m_Progress.travel; }
    float GetHoldElapsed() const { return m_Progress.holdElapsed; }
    void RestoreProgress(int travel, float holdElapsed);

    float GetArcAngle() const;
    void Advance(float seconds);
    void Update(float frameSeconds);

    virtual void SaveSettings(IniFile::Values& values) const override;
    virtual void LoadSettings(const IniFile::Values& values) override;

private:
    struct Progress
    {
        float leg = 0.0f;
        int travel = 1;
        float holdElapsed = 0.0f;
    };

    struct WrittenPlace
    {
        bool valid = false;
        Math::Vector3 direction;
        float axisAzimuth = 0.0f;
        float minElevation = 0.0f;
        Easing easing = Easing::Linear;
    };

    static Settings GetDefaultSettings();
    void FollowLight();
    void WriteLight();

    LightComponent& m_Light;
    Settings m_Settings;
    Progress m_Progress;
    WrittenPlace m_Written;
};

void RegisterSunPath(SunPathComponent& sunPath);
void UnregisterSunPath(void);
void ShowSunPathDetails(SunPathComponent& sunPath);
