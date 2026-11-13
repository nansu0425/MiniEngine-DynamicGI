#ifndef __PROBE_VISIBILITY_HLSLI__
#define __PROBE_VISIBILITY_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"

static const uint kProbeVisibilityNone = 0;
static const uint kProbeVisibilityShadowRay = 1;
static const uint kProbeVisibilityChebyshev = 2;

bool CanShadowRayReachProbe(IrradianceProbeGrid grid, uint3 coordinate, float3 position, float3 geometryNormal)
{
    const float3 toProbe = GetProbePosition(grid, coordinate) - position;
    const float distance = length(toProbe);
    RayDesc ray;
    if (!DescribeSurfaceRay(position, geometryNormal, toProbe / distance, ray))
        return false;
    ray.TMax = distance;
    return !IsOccluded(ray, kCullSingleSidedBackFaces);
}

float GetChebyshevVisibility(Texture2D<float2> distanceAtlas, SamplerState linearClamp, IrradianceProbeGrid grid, uint3 coordinate, float3 position)
{
    const float3 fromProbe = position - GetProbePosition(grid, coordinate);
    const float distance = length(fromProbe);
    const float2 moments = distanceAtlas.SampleLevel(linearClamp, GetProbeAtlasUV(grid, coordinate, fromProbe / distance), 0.0);
    if (distance <= moments.x)
        return 1.0;
    const float variance = max(moments.y - moments.x * moments.x, 0.0);
    const float excess = distance - moments.x;
    return variance / (variance + excess * excess);
}

float GetProbeVisibilityWeight(uint visibility, Texture2D<float2> distanceAtlas, SamplerState linearClamp, IrradianceProbeGrid grid, uint3 coordinate,
    float3 position, float3 geometryNormal)
{
    if (visibility == kProbeVisibilityShadowRay)
        return CanShadowRayReachProbe(grid, coordinate, position, geometryNormal) ? 1.0 : 0.0;
    if (visibility == kProbeVisibilityChebyshev)
        return GetChebyshevVisibility(distanceAtlas, linearClamp, grid, coordinate, position);
    return 1.0;
}

float3 SampleVisibleIrradianceProbes(uint visibility, Texture2D<float4> irradianceAtlas, Texture2D<float2> distanceAtlas, SamplerState linearClamp,
    IrradianceProbeGrid grid, float3 position, float3 geometryNormal, float3 direction)
{
    float3 irradiance = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        const IrradianceProbeCorner probe = GetIrradianceProbeCorner(grid, position, corner);
        const float weight = probe.trilinearWeight *
            GetProbeVisibilityWeight(visibility, distanceAtlas, linearClamp, grid, probe.coordinate, position, geometryNormal);
        if (weight == 0.0)
            continue;
        irradiance += weight * SampleProbeIrradiance(irradianceAtlas, linearClamp, grid, probe.coordinate, direction);
        weightSum += weight;
    }
    if (weightSum == 0.0)
        return SampleIrradianceProbes(irradianceAtlas, linearClamp, grid, position, direction);
    return irradiance / weightSum;
}

#endif
