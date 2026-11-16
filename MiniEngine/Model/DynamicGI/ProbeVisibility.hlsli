#ifndef __PROBE_VISIBILITY_HLSLI__
#define __PROBE_VISIBILITY_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"

static const uint kProbeVisibilityNone = 0;
static const uint kProbeVisibilityShadowRay = 1;
static const uint kProbeVisibilityChebyshev = 2;

struct ProbeDistanceAtlas
{
    Texture2D<float2> moments;
    uint texelsPerSide;
};

ProbeDistanceAtlas MakeProbeDistanceAtlas(Texture2D<float2> moments, uint texelsPerSide)
{
    ProbeDistanceAtlas atlas;
    atlas.moments = moments;
    atlas.texelsPerSide = texelsPerSide;
    return atlas;
}

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

struct ProbeDistanceSample
{
    float distance;
    float2 moments;
};

ProbeDistanceSample SampleProbeDistance(ProbeDistanceAtlas distanceAtlas, SamplerState linearClamp, IrradianceProbeGrid grid, uint3 coordinate,
    float3 position)
{
    const float3 fromProbe = position - GetProbePosition(grid, coordinate);
    ProbeDistanceSample result;
    result.distance = length(fromProbe);
    result.moments =
        distanceAtlas.moments.SampleLevel(linearClamp, GetProbeAtlasUV(grid, distanceAtlas.texelsPerSide, coordinate, fromProbe / result.distance), 0.0);
    return result;
}

float GetChebyshevVisibility(ProbeDistanceSample distanceSample)
{
    const float mean = distanceSample.moments.x;
    if (distanceSample.distance <= mean)
        return 1.0;
    const float variance = max(distanceSample.moments.y - mean * mean, 0.0);
    const float excess = distanceSample.distance - mean;
    return variance / (variance + excess * excess);
}

float GetProbeVisibilityWeight(uint visibility, ProbeDistanceAtlas distanceAtlas, SamplerState linearClamp, IrradianceProbeGrid grid, uint3 coordinate,
    float3 position, float3 geometryNormal)
{
    if (visibility == kProbeVisibilityShadowRay)
        return CanShadowRayReachProbe(grid, coordinate, position, geometryNormal) ? 1.0 : 0.0;
    if (visibility == kProbeVisibilityChebyshev)
        return GetChebyshevVisibility(SampleProbeDistance(distanceAtlas, linearClamp, grid, coordinate, position));
    return 1.0;
}

#endif
