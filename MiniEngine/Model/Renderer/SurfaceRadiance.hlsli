#ifndef __SURFACE_RADIANCE_HLSLI__
#define __SURFACE_RADIANCE_HLSLI__

#include "Model/Renderer/RasterSurface.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/SurfaceShading.hlsli"

float3 GetOutgoingRadiance(RasterSurface surface, float3 view, float3 shadowCoord, Texture2D<float> shadowMap, SamplerComparisonState shadowMapSampler,
    float3 lightDirection, float3 lightIlluminance, float3 indirectDiffuseRadiance)
{
    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, view);

    // direct light
    const float lightVisibility = GetDirectionalLightVisibility(shadowMap, shadowMapSampler, shadowCoord, surface.geometryNormal, lightDirection);
    const float3 directRadiance = EvaluateGltfBrdf(brdf, lightDirection) * lightIlluminance * (max(0.0, dot(surface.shadingNormal, lightDirection)) * lightVisibility);

    // TODO: indirect specular light

    return surface.emissive + directRadiance + indirectDiffuseRadiance;
}

#endif
