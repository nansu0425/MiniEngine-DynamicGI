#ifndef __PATH_TRACING_HLSLI__
#define __PATH_TRACING_HLSLI__

#include "Model/Raytracing/HitSurface.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"
#include "Core/Shaders/LambertBrdf.hlsli"
#include "Core/Shaders/Random.hlsli"
#include "Core/Shaders/SurfaceShading.hlsli"

struct PathTracingLights
{
    float3 lightDirection;
    float3 lightIlluminance;
    float3 skyRadiance;
    float4x4 shadowMatrix;
};

static const uint kFirstRussianRouletteHit = 3;

float3 TraceIncomingRadiance(RayDesc ray, PathTracingLights lights, Texture2D<float> shadowMap, SamplerComparisonState shadowMapSampler,
    uint maxHits, bool russianRoulette, inout Random random)
{
    const bool lit = any(lights.lightIlluminance != 0.0);
    float3 radiance = 0.0;
    float3 throughput = 1.0;
    for (uint hits = 1;; ++hits)
    {
        RayHit rayHit;
        if (!TraceClosestHit(ray, kCullSingleSidedBackFaces, rayHit))
            return radiance + throughput * lights.skyRadiance;

        const HitSurface surface = GetHitSurface(rayHit, kSharpestTextureLod);
        const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, -ray.Direction);
        const float3 shadowCoord = mul(lights.shadowMatrix, float4(surface.position, 1.0)).xyz;
        const float visibility = lit ? GetDirectionalLightVisibility(shadowMap, shadowMapSampler, shadowCoord, surface.geometryNormal, lights.lightDirection) : 0.0;
        const float3 directRadiance = EvaluateGltfBrdf(brdf, lights.lightDirection) * lights.lightIlluminance * (max(0.0, dot(surface.shadingNormal, lights.lightDirection)) * visibility);

        radiance += throughput * (surface.emissive + directRadiance);

        if (hits == maxHits)
            return radiance;

        const float3 direction = SampleLambertDirection(surface.shadingNormal, random.rand2());
        if (!DescribeSurfaceRay(surface.position, surface.geometryNormal, direction, ray))
            return radiance;

        throughput *= kPi * EvaluateGltfDiffuseLobe(brdf, direction);

        if (russianRoulette && hits >= kFirstRussianRouletteHit)
        {
            const float survival = min(1.0, max(throughput.r, max(throughput.g, throughput.b)));
            if (random.rand() >= survival)
                return radiance;
            throughput /= survival;
        }
    }
}

#endif
