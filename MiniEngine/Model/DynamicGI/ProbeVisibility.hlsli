#ifndef __PROBE_VISIBILITY_HLSLI__
#define __PROBE_VISIBILITY_HLSLI__

#include "Model/DynamicGI/ProbeVolume.hlsli"
#include "Model/DynamicGI/ProbeLookupSurface.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"

static const uint kProbeVisibilityNone = 0;
static const uint kProbeVisibilityShadowRay = 1;
static const uint kProbeVisibilityChebyshev = 2;

bool CanShadowRayReachProbe(ProbePositions positions, uint3 coordinate, float3 position, float3 geometryNormal)
{
    const float3 toProbe = GetProbePosition(positions, coordinate) - position;
    const float distance = length(toProbe);
    RayDesc ray;
    if (!DescribeSurfaceRay(position, geometryNormal, toProbe / distance, ray))
        return false;
    ray.TMax = distance;
    return !IsOccluded(ray, kCullSingleSidedBackFaces);
}

struct ProbeDistanceSample
{
    float distance;
    float2 moments;
};

ProbeDistanceSample SampleProbeDistance(ProbeVolume volume, SamplerState linearClamp, uint3 coordinate, float3 position)
{
    const float3 fromProbe = position - GetProbePosition(volume.positions, coordinate);
    ProbeDistanceSample result;
    result.distance = length(fromProbe);
    const float2 uv = GetProbeAtlasUV(volume.positions.grid, volume.distanceAtlas.texelsPerSide, coordinate, fromProbe / result.distance);
    result.moments = volume.distanceAtlas.moments.SampleLevel(linearClamp, uv, 0.0);
    return result;
}

float GetChebyshevVisibility(float exponent, ProbeDistanceSample distanceSample)
{
    const float mean = distanceSample.moments.x;
    if (distanceSample.distance <= mean)
        return 1.0;
    const float variance = max(distanceSample.moments.y - mean * mean, 0.0);
    const float excess = distanceSample.distance - mean;
    return pow(variance / (variance + excess * excess), exponent);
}

float3 GetDistanceLookupPosition(float selfShadowBias, IrradianceProbeGrid grid, ProbeLookupSurface surface)
{
    const float shortestSpacing = min(grid.spacing.x, min(grid.spacing.y, grid.spacing.z));
    return surface.position + surface.geometryNormal * (selfShadowBias * shortestSpacing);
}

float GetProbeVisibilityWeight(uint visibility, float chebyshevExponent, ProbeVolume volume, SamplerState linearClamp, uint3 coordinate,
    ProbeLookupSurface surface, float3 distanceLookupPosition)
{
    if (visibility == kProbeVisibilityShadowRay)
        return CanShadowRayReachProbe(volume.positions, coordinate, surface.position, surface.geometryNormal) ? 1.0 : 0.0;
    if (visibility == kProbeVisibilityChebyshev)
        return GetChebyshevVisibility(chebyshevExponent, SampleProbeDistance(volume, linearClamp, coordinate, distanceLookupPosition));
    return 1.0;
}

#endif
