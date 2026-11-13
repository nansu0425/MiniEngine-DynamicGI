#ifndef __PROBE_VISIBILITY_HLSLI__
#define __PROBE_VISIBILITY_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"

static const uint kProbeVisibilityNone = 0;
static const uint kProbeVisibilityShadowRay = 1;

bool IsProbeVisible(IrradianceProbeGrid grid, uint3 coordinate, float3 position, float3 geometryNormal)
{
    const float3 toProbe = GetProbePosition(grid, coordinate) - position;
    const float distance = length(toProbe);
    RayDesc ray;
    if (!DescribeSurfaceRay(position, geometryNormal, toProbe / distance, ray))
        return false;
    ray.TMax = distance;
    return !IsOccluded(ray, kCullSingleSidedBackFaces);
}

float3 SampleVisibleIrradianceProbes(Texture2D<float4> atlas, SamplerState linearClamp, IrradianceProbeGrid grid, float3 position,
    float3 geometryNormal, float3 direction)
{
    float3 irradiance = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        const IrradianceProbeCorner probe = GetIrradianceProbeCorner(grid, position, corner);
        if (!IsProbeVisible(grid, probe.coordinate, position, geometryNormal))
            continue;
        irradiance += probe.trilinearWeight * SampleProbeIrradiance(atlas, linearClamp, grid, probe.coordinate, direction);
        weightSum += probe.trilinearWeight;
    }
    if (weightSum == 0.0)
        return SampleIrradianceProbes(atlas, linearClamp, grid, position, direction);
    return irradiance / weightSum;
}

#endif
