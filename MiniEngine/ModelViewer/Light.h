#pragma once

#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Math/Matrix3.h"
#include "Core/Scene/Scene.h"
#include "Model/Lights/SceneLights.h"

#include <optional>
#include <string>
#include <vector>

class ModelInstance;

class LightComponent : public Scene::Component
{
public:
    enum class Type { Directional, Point, Spot };

    struct Properties
    {
        // 조명이 쓰는 +Z를 계산 없이 그대로 읽도록 quaternion 대신 직교 matrix로 둔다.
        Math::Matrix3 rotation;
        Math::Vector3 position;
        Math::Vector3 color;
        float intensity;
        float innerConeAngle;
        float outerConeAngle;
        std::optional<float> range;
    };

    explicit LightComponent(Type type);

    Type GetType() const { return m_Type; }
    const Properties& GetProperties() const { return m_Properties; }
    void SetProperties(const Properties& properties) { m_Properties = properties; }

    Math::Vector3 GetDirection() const { return m_Properties.rotation.GetZ(); }
    Math::Vector3 GetIlluminance() const
    {
        ASSERT(m_Type == Type::Directional);
        return m_Properties.color * Math::Scalar(m_Properties.intensity);
    }
    Math::Vector3 GetLuminousIntensity() const
    {
        ASSERT(m_Type != Type::Directional);
        return m_Properties.color * Math::Scalar(m_Properties.intensity);
    }

    void ResetTransform();

    virtual void SaveSettings(IniFile::Values& values) const override;
    virtual void LoadSettings(const IniFile::Values& values) override;

private:
    static std::vector<std::string> ListSettingKeys(Type type);
    static Properties GetDefaultProperties(Type type);

    Type m_Type;
    Properties m_Properties;
};

constexpr char kPointLightName[] = "Point Light";
constexpr char kSpotLightName[] = "Spot Light";

// spec의 기본 intensity는 1 lux이지만, 기본 화면과 harness baseline의 Exposure가 맞춰 둔 밝기를 지키도록 4π로 둔다. 그 Exposure를
// 맞출 때의 raster는 diffuse에 1/π가 없어 intensity 4로 지금의 4π만큼 밝았다.
constexpr float kDefaultDirectionalLightIntensity = 4.0f * DirectX::XM_PI;

bool IsColor(Math::Vector3 color);

LightComponent& AssembleLight(Scene::Object& object, LightComponent::Type type, const ModelInstance& model);
LightComponent* FindLocalLight(const Scene::Object& object);
LocalLight DescribeLocalLightInWorldUnits(const LightComponent& light, float worldUnitsPerMetre);

void RegisterDirectionalLight(LightComponent& light);
void RegisterLocalLights(void);
void ShowLightDetails(LightComponent& light);

// directional light의 rotation은 user.ini처럼 +Z와 +Y로 적어, 다시 읽으면 bit 단위로 같은 rotation이 된다. position은 조명에 영향이 없어 적지 않는다.
void DescribeDirectionalLightConditions(const LightComponent& light, MeasurementConditions::Values& values);
void ReadDirectionalLightConditions(LightComponent& light, const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading);
void DescribeLocalLightConditions(MeasurementConditions::Values& values);
void ReadLocalLightConditions(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading);
