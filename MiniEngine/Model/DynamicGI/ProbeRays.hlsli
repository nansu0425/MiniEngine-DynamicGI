#ifndef __PROBE_RAYS_HLSLI__
#define __PROBE_RAYS_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

struct ProbeRays
{
    IrradianceProbeGrid grid;
    float4x4 rotation;
    uint raysPerProbe;
};

static const float kProbeRayMaxDistancePerSpacing = 1.5;

float GetProbeRayMaxDistance(IrradianceProbeGrid grid)
{
    return kProbeRayMaxDistancePerSpacing * length(grid.spacing);
}

float3 GetSphericalFibonacciDirection(uint index, uint count)
{
    static const float kGoldenRatioFraction = 0.6180339887;
    static const float kTwoPi = 6.283185307;
    const float z = 1.0 - (2.0 * index + 1.0) / count;
    const float radius = sqrt(saturate(1.0 - z * z));
    const float azimuth = kTwoPi * frac(index * kGoldenRatioFraction);
    return float3(radius * cos(azimuth), radius * sin(azimuth), z);
}

float3 GetProbeRayDirection(ProbeRays rays, uint ray)
{
    return normalize(mul((float3x3)rays.rotation, GetSphericalFibonacciDirection(ray, rays.raysPerProbe)));
}

#endif
