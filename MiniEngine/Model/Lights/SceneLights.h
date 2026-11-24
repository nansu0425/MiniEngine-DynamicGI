#pragma once

#include "Core/pch.h"

constexpr uint32_t kMaxLocalLights = 8;

__declspec(align(16)) struct LocalLight
{
    DirectX::XMFLOAT3 position;
    float inverseRange;
    DirectX::XMFLOAT3 intensity;
    float spotAngleScale;
    DirectX::XMFLOAT3 emissionDirection;
    float spotAngleOffset;
};

__declspec(align(16)) struct SceneLights
{
    // surface에서 directional light를 향하는 방향과, light에 수직인 면의 illuminance (lux)
    Math::Vector3 directionalLightDirection;
    Math::Vector3 directionalLightIlluminance;
    // sky의 radiance (cd/m²). 모든 방향에서 같다.
    Math::Vector3 skyRadiance;
    LocalLight localLights[kMaxLocalLights];
    uint32_t localLightCount;
};
