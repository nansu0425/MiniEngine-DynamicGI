#ifndef __PROBE_WEIGHTING_HLSLI__
#define __PROBE_WEIGHTING_HLSLI__

#include "Model/DynamicGI/ProbeVisibility.hlsli"
#include "Model/DynamicGI/ProbeFacing.hlsli"
#include "Model/DynamicGI/ProbeClassification.hlsli"

struct ProbeWeighting
{
    uint visibility;
    float selfShadowBias;
    float chebyshevExponent;
    float weightCrushThreshold;
    uint facing;
    float minFacingWeight;
    uint facingNormal;
    uint classification;
    float maxBackFaceRatio;
};

bool InterpolatesTrilinearly(ProbeWeighting weighting)
{
    return weighting.visibility == kProbeVisibilityNone && weighting.facing == 0 && weighting.classification == 0;
}

float CrushWeakProbeWeight(float threshold, float weight)
{
    if (weight >= threshold)
        return weight;
    return weight * weight * weight / (threshold * threshold);
}

struct WeightedProbeCorners
{
    float3 distanceLookupPosition;
    IrradianceProbeCorner corners[kNumProbeCorners];
    float activeWeights[kNumProbeCorners];
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
    probes.distanceLookupPosition = weighting.visibility == kProbeVisibilityChebyshev ?
        GetDistanceLookupPosition(weighting.selfShadowBias, volume.grid, surface) : surface.position;
    probes.weightSum = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        probes.corners[corner] = GetIrradianceProbeCorner(volume.grid, surface.position, corner);
        probes.activeWeights[corner] = GetProbeActiveWeight(weighting.classification, weighting.maxBackFaceRatio, volume.backFaceRatios, volume.grid,
            probes.corners[corner].coordinate);
        probes.visibilityWeights[corner] = GetProbeVisibilityWeight(weighting.visibility, weighting.chebyshevExponent, volume, linearClamp,
            probes.corners[corner].coordinate, surface, probes.distanceLookupPosition);
        probes.facingWeights[corner] = weighting.facing != 0 ?
            GetProbeFacingWeight(weighting.minFacingWeight, volume.grid, probes.corners[corner].coordinate, surface.position, facingNormal) : 1.0;
        const float occlusionWeight = CrushWeakProbeWeight(weighting.weightCrushThreshold,
            probes.activeWeights[corner] * probes.visibilityWeights[corner] * probes.facingWeights[corner]);
        probes.weights[corner] = probes.corners[corner].trilinearWeight * occlusionWeight;
        probes.weightSum += probes.weights[corner];
    }

    probes.fallsBackToTrilinear = probes.weightSum == 0.0;
    if (probes.fallsBackToTrilinear)
    {
        [unroll]
        for (uint corner = 0; corner < kNumProbeCorners; ++corner)
        {
            probes.weights[corner] = probes.corners[corner].trilinearWeight * probes.activeWeights[corner];
            probes.weightSum += probes.weights[corner];
        }
        if (probes.weightSum == 0.0)
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
