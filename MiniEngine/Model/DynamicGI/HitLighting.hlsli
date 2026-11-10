#ifndef __HIT_LIGHTING_HLSLI__
#define __HIT_LIGHTING_HLSLI__

#include "Model/Raytracing/HitSurface.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/SurfaceShading.hlsli"

struct GILights
{
    float3 lightDirection;
    float3 lightIlluminance;
    float3 skyRadiance;
    float4x4 shadowMatrix;
};

float3 GetHitDirectRadiance(HitSurface surface, GltfBrdfInput brdf, GILights lights, Texture2D<float> shadowMap, SamplerComparisonState shadowMapSampler)
{
    const bool lit = any(lights.lightIlluminance != 0.0);
    const float3 shadowCoord = mul(lights.shadowMatrix, float4(surface.position, 1.0)).xyz;
    const float visibility = lit ? GetDirectionalLightVisibility(shadowMap, shadowMapSampler, shadowCoord, surface.geometryNormal, lights.lightDirection) : 0.0;
    return EvaluateGltfBrdf(brdf, lights.lightDirection) * lights.lightIlluminance * (max(0.0, dot(surface.shadingNormal, lights.lightDirection)) * visibility);
}

#endif
