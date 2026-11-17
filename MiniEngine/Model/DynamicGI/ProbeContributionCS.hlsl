#include "Model/Renderer/GBufferTextures.hlsli"
#include "Model/DynamicGI/ProbeWeighting.hlsli"
#include "Model/DynamicGI/ProbeContribution.hlsli"

cbuffer Constants : register(b0)
{
    uint2 Pixel;
    uint CapturesSurface;
    uint DistanceTexelsPerSide;
    ProbeWeighting Weighting;
    IrradianceProbeGrid Grid;
};

Texture2D<float4> ProbesIrradiance : register(t5);
Texture2D<float2> ProbesDistance : register(t6);
Texture2D<float> ProbesBackFaceRatio : register(t7);
Texture2D<float4> ProbesOffset : register(t8);
SamplerState LinearClamp : register(s0);
RWStructuredBuffer<SurfaceProbeContribution> Contribution : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    SurfaceProbeContribution result = Contribution[0];
    if (CapturesSurface != 0)
    {
        RasterSurface surface;
        result.hasSurface = LoadGBuffer(Pixel, surface);
        result.position = surface.position;
        result.geometryNormal = surface.geometryNormal;
        result.shadingNormal = surface.shadingNormal;
    }
    if (result.hasSurface == 0)
    {
        Contribution[0] = result;
        return;
    }

    const ProbeDistanceAtlas distanceAtlas = MakeProbeDistanceAtlas(ProbesDistance, DistanceTexelsPerSide);
    const ProbeVolume volume = MakeProbeVolume(MakeProbePositions(Grid, ProbesOffset), ProbesIrradiance, distanceAtlas, ProbesBackFaceRatio);
    const ProbeLookupSurface lookupSurface = MakeProbeLookupSurface(result.position, result.geometryNormal, result.shadingNormal);
    const WeightedProbeCorners probes = GetWeightedProbeCorners(Weighting, volume, LinearClamp, lookupSurface);
    result.distanceLookupPosition = probes.distanceLookupPosition;
    result.comparesDistance = Weighting.visibility == kProbeVisibilityChebyshev;
    result.fallsBackToTrilinear = probes.fallsBackToTrilinear;
    result.irradiance = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        ProbeCornerContribution contribution;
        contribution.coordinate = probes.corners[corner].coordinate;
        contribution.position = GetProbePosition(volume.positions, contribution.coordinate);
        contribution.trilinearWeight = probes.corners[corner].trilinearWeight;
        contribution.backFaceRatio = GetProbeBackFaceRatio(ProbesBackFaceRatio, Grid, contribution.coordinate);
        contribution.activeWeight = probes.activeWeights[corner];
        contribution.visibilityWeight = probes.visibilityWeights[corner];
        contribution.facingWeight = probes.facingWeights[corner];
        contribution.weight = probes.weights[corner] / probes.weightSum;
        contribution.irradiance = SampleProbeIrradiance(ProbesIrradiance, LinearClamp, Grid, contribution.coordinate, result.shadingNormal);
        const ProbeDistanceSample distanceSample = SampleProbeDistance(volume, LinearClamp, contribution.coordinate, result.distanceLookupPosition);
        contribution.distance = distanceSample.distance;
        contribution.distanceMean = distanceSample.moments.x;
        result.irradiance += contribution.weight * contribution.irradiance;
        result.corners[corner] = contribution;
    }

    result.shadingIrradiance = InterpolatesTrilinearly(Weighting) ?
        SampleIrradianceProbes(ProbesIrradiance, LinearClamp, Grid, result.position, result.shadingNormal) :
        SampleWeightedIrradianceProbes(Weighting, volume, LinearClamp, lookupSurface);
    Contribution[0] = result;
}
