#ifndef __PROBE_WEIGHTING_HLSLI__
#define __PROBE_WEIGHTING_HLSLI__

#include "Model/DynamicGI/ProbeVisibility.hlsli"
#include "Model/DynamicGI/ProbeFacing.hlsli"

struct ProbeWeighting
{
    uint visibility;
    uint facing;
    float minFacingWeight;
    uint facingNormal;
};

bool InterpolatesTrilinearly(ProbeWeighting weighting)
{
    return weighting.visibility == kProbeVisibilityNone && weighting.facing == 0;
}

struct ProbeLookupSurface
{
    float3 position;
    float3 geometryNormal;
    float3 shadingNormal;
};

ProbeLookupSurface MakeProbeLookupSurface(float3 position, float3 geometryNormal, float3 shadingNormal)
{
    ProbeLookupSurface surface;
    surface.position = position;
    surface.geometryNormal = geometryNormal;
    surface.shadingNormal = shadingNormal;
    return surface;
}

struct WeightedProbeCorners
{
    IrradianceProbeCorner corners[kNumProbeCorners];
    float visibilityWeights[kNumProbeCorners];
    float facingWeights[kNumProbeCorners];
    float weights[kNumProbeCorners];
    float weightSum;
    bool fallsBackToTrilinear;
};

WeightedProbeCorners GetWeightedProbeCorners(ProbeWeighting weighting, ProbeVolume volume, SamplerState linearClamp, ProbeLookupSurface surface)
{
    const float3 facingNormal = weighting.facingNormal == kProbeFacingNormalShading ? surface.shadingNormal : surface.geometryNormal;
    WeightedProbeCorners probes;
    probes.weightSum = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        probes.corners[corner] = GetIrradianceProbeCorner(volume.grid, surface.position, corner);
        probes.visibilityWeights[corner] = GetProbeVisibilityWeight(weighting.visibility, volume, linearClamp, probes.corners[corner].coordinate,
            surface.position, surface.geometryNormal);
        probes.facingWeights[corner] = weighting.facing != 0 ?
            GetProbeFacingWeight(weighting.minFacingWeight, volume.grid, probes.corners[corner].coordinate, surface.position, facingNormal) : 1.0;
        probes.weights[corner] = probes.corners[corner].trilinearWeight * probes.visibilityWeights[corner] * probes.facingWeights[corner];
        probes.weightSum += probes.weights[corner];
    }

    probes.fallsBackToTrilinear = probes.weightSum == 0.0;
    if (probes.fallsBackToTrilinear)
    {
        [unroll]
        for (uint corner = 0; corner < kNumProbeCorners; ++corner)
            probes.weights[corner] = probes.corners[corner].trilinearWeight;
        probes.weightSum = 1.0;
    }
    return probes;
}

float3 SampleWeightedIrradianceProbes(ProbeWeighting weighting, ProbeVolume volume, SamplerState linearClamp, ProbeLookupSurface surface)
{
    const WeightedProbeCorners probes = GetWeightedProbeCorners(weighting, volume, linearClamp, surface);
    float3 irradiance = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        if (probes.weights[corner] == 0.0)
            continue;
        const float3 probeIrradiance =
            SampleProbeIrradiance(volume.irradianceAtlas, linearClamp, volume.grid, probes.corners[corner].coordinate, surface.shadingNormal);
        irradiance += probes.weights[corner] * probeIrradiance;
    }
    return irradiance / probes.weightSum;
}

#endif
