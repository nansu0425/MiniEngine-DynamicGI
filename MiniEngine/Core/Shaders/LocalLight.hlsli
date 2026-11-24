#ifndef __LOCAL_LIGHT_HLSLI__
#define __LOCAL_LIGHT_HLSLI__

#include "Core/Shaders/SceneLights.hlsli"

struct LocalLightIncidence
{
    float3 direction;
    float3 illuminance;
};

LocalLightIncidence GetLocalLightIncidence(LocalLight light, float3 position)
{
    const float3 toLight = light.position - position;
    const float distanceSquared = dot(toLight, toLight);
    const float rangeRatioSquared = distanceSquared * light.inverseRange * light.inverseRange;
    const float rangeWindow = saturate(1.0 - rangeRatioSquared * rangeRatioSquared);

    LocalLightIncidence incidence;
    incidence.direction = toLight * rsqrt(distanceSquared);
    const float spotFalloff = saturate(dot(light.emissionDirection, -incidence.direction) * light.spotAngleScale + light.spotAngleOffset);
    incidence.illuminance = light.intensity * (rangeWindow * spotFalloff * spotFalloff / distanceSquared);
    return incidence;
}

#endif
