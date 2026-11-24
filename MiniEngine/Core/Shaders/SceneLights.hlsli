#ifndef __SCENE_LIGHTS_HLSLI__
#define __SCENE_LIGHTS_HLSLI__

static const uint kMaxLocalLights = 8;

struct LocalLight
{
    float3 position;
    float inverseRange;
    float3 intensity;
    float spotAngleScale;
    float3 emissionDirection;
    float spotAngleOffset;
};

struct SceneLights
{
    float3 directionalLightDirection;
    float3 directionalLightIlluminance;
    float3 skyRadiance;
    LocalLight localLights[kMaxLocalLights];
    uint localLightCount;
};

#endif
