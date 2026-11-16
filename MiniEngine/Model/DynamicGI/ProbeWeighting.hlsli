#ifndef __PROBE_WEIGHTING_HLSLI__
#define __PROBE_WEIGHTING_HLSLI__

#include "Model/DynamicGI/ProbeVisibility.hlsli"
#include "Model/DynamicGI/ProbeFacing.hlsli"

struct ProbeWeighting
{
    uint visibility;
    uint facing;
    float minFacingWeight;
};

bool InterpolatesTrilinearly(ProbeWeighting weighting)
{
    return weighting.visibility == kProbeVisibilityNone && weighting.facing == 0;
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

WeightedProbeCorners GetWeightedProbeCorners(ProbeWeighting weighting, ProbeDistanceAtlas distanceAtlas, SamplerState linearClamp,
    IrradianceProbeGrid grid, float3 position, float3 geometryNormal)
{
    WeightedProbeCorners probes;
    probes.weightSum = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        probes.corners[corner] = GetIrradianceProbeCorner(grid, position, corner);
        probes.visibilityWeights[corner] = GetProbeVisibilityWeight(weighting.visibility, distanceAtlas, linearClamp, grid,
            probes.corners[corner].coordinate, position, geometryNormal);
        probes.facingWeights[corner] = weighting.facing != 0 ?
            GetProbeFacingWeight(weighting.minFacingWeight, grid, probes.corners[corner].coordinate, position, geometryNormal) : 1.0;
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

float3 SampleWeightedIrradianceProbes(ProbeWeighting weighting, Texture2D<float4> irradianceAtlas, ProbeDistanceAtlas distanceAtlas,
    SamplerState linearClamp, IrradianceProbeGrid grid, float3 position, float3 geometryNormal, float3 direction)
{
    const WeightedProbeCorners probes = GetWeightedProbeCorners(weighting, distanceAtlas, linearClamp, grid, position, geometryNormal);
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
