#ifndef __HIT_LIGHTING_HLSLI__
#define __HIT_LIGHTING_HLSLI__

#include "Model/Raytracing/HitSurface.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/SurfaceShading.hlsli"
#include "Core/Shaders/SceneLights.hlsli"

struct GILights
{
    SceneLights sceneLights;
    float4x4 shadowMatrix;
};

float3 GetHitDirectRadiance(HitSurface surface, GltfBrdfInput brdf, GILights lights, Texture2D<float> shadowMap, SamplerComparisonState shadowMapSampler)
{
    const bool lit = any(lights.sceneLights.lightIlluminance != 0.0);
    const float3 shadowCoord = mul(lights.shadowMatrix, float4(surface.position, 1.0)).xyz;
    const float visibility = lit ? GetDirectionalLightVisibility(shadowMap, shadowMapSampler, shadowCoord, surface.vertexNormal, lights.sceneLights.lightDirection) : 0.0;
    return EvaluateGltfBrdf(brdf, lights.sceneLights.lightDirection) * lights.sceneLights.lightIlluminance * (max(0.0, dot(surface.shadingNormal, lights.sceneLights.lightDirection)) * visibility);
}

#endif
