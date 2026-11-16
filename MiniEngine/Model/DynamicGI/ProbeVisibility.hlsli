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

float GetChebyshevVisibility(ProbeDistanceAtlas distanceAtlas, SamplerState linearClamp, IrradianceProbeGrid grid, uint3 coordinate, float3 position)
{
    const float3 fromProbe = position - GetProbePosition(grid, coordinate);
    const float distance = length(fromProbe);
    const float2 moments =
        distanceAtlas.moments.SampleLevel(linearClamp, GetProbeAtlasUV(grid, distanceAtlas.texelsPerSide, coordinate, fromProbe / distance), 0.0);
    if (distance <= moments.x)
        return 1.0;
    const float variance = max(moments.y - moments.x * moments.x, 0.0);
    const float excess = distance - moments.x;
    return variance / (variance + excess * excess);
}

float GetProbeVisibilityWeight(uint visibility, ProbeDistanceAtlas distanceAtlas, SamplerState linearClamp, IrradianceProbeGrid grid, uint3 coordinate,
    float3 position, float3 geometryNormal)
{
    if (visibility == kProbeVisibilityShadowRay)
        return CanShadowRayReachProbe(grid, coordinate, position, geometryNormal) ? 1.0 : 0.0;
    if (visibility == kProbeVisibilityChebyshev)
        return GetChebyshevVisibility(distanceAtlas, linearClamp, grid, coordinate, position);
    return 1.0;
}

struct VisibleProbeCorners
{
    IrradianceProbeCorner corners[kNumProbeCorners];
    float visibilityWeights[kNumProbeCorners];
    float weights[kNumProbeCorners];
    float weightSum;
    bool ignoresVisibility;
};

VisibleProbeCorners GetVisibleProbeCorners(uint visibility, ProbeDistanceAtlas distanceAtlas, SamplerState linearClamp, IrradianceProbeGrid grid,
    float3 position, float3 geometryNormal)
{
    VisibleProbeCorners probes;
    probes.weightSum = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        probes.corners[corner] = GetIrradianceProbeCorner(grid, position, corner);
        probes.visibilityWeights[corner] =
            GetProbeVisibilityWeight(visibility, distanceAtlas, linearClamp, grid, probes.corners[corner].coordinate, position, geometryNormal);
        probes.weights[corner] = probes.corners[corner].trilinearWeight * probes.visibilityWeights[corner];
        probes.weightSum += probes.weights[corner];
    }

    probes.ignoresVisibility = probes.weightSum == 0.0;
    if (probes.ignoresVisibility)
    {
        [unroll]
        for (uint corner = 0; corner < kNumProbeCorners; ++corner)
            probes.weights[corner] = probes.corners[corner].trilinearWeight;
        probes.weightSum = 1.0;
    }
    return probes;
}

float3 SampleVisibleIrradianceProbes(uint visibility, Texture2D<float4> irradianceAtlas, ProbeDistanceAtlas distanceAtlas, SamplerState linearClamp,
    IrradianceProbeGrid grid, float3 position, float3 geometryNormal, float3 direction)
{
    const VisibleProbeCorners probes = GetVisibleProbeCorners(visibility, distanceAtlas, linearClamp, grid, position, geometryNormal);
    float3 irradiance = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        if (probes.weights[corner] == 0.0)
            continue;
        irradiance += probes.weights[corner] * SampleProbeIrradiance(irradianceAtlas, linearClamp, grid, probes.corners[corner].coordinate, direction);
    }
    return irradiance / probes.weightSum;
}

#endif
