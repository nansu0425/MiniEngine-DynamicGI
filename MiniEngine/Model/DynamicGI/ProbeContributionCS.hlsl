#include "Model/Renderer/GBufferTextures.hlsli"
#include "Model/DynamicGI/ProbeVisibility.hlsli"
#include "Model/DynamicGI/ProbeContribution.hlsli"

cbuffer Constants : register(b0)
{
    uint2 Pixel;
    uint CapturesSurface;
    uint Visibility;
    uint DistanceTexelsPerSide;
    IrradianceProbeGrid Grid;
};

Texture2D<float4> ProbesIrradiance : register(t5);
Texture2D<float2> ProbesDistance : register(t6);
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
    const VisibleProbeCorners probes = GetVisibleProbeCorners(Visibility, distanceAtlas, LinearClamp, Grid, result.position, result.geometryNormal);
    result.comparesDistance = Visibility == kProbeVisibilityChebyshev;
    result.ignoresVisibility = probes.ignoresVisibility;
    result.irradiance = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        ProbeCornerContribution contribution;
        contribution.coordinate = probes.corners[corner].coordinate;
        contribution.trilinearWeight = probes.corners[corner].trilinearWeight;
        contribution.visibilityWeight = probes.visibilityWeights[corner];
        contribution.weight = probes.weights[corner] / probes.weightSum;
        contribution.irradiance = SampleProbeIrradiance(ProbesIrradiance, LinearClamp, Grid, contribution.coordinate, result.shadingNormal);
        const ProbeDistanceSample distanceSample = SampleProbeDistance(distanceAtlas, LinearClamp, Grid, contribution.coordinate, result.position);
        contribution.distance = distanceSample.distance;
        contribution.distanceMean = distanceSample.moments.x;
        result.irradiance += contribution.weight * contribution.irradiance;
        result.corners[corner] = contribution;
    }

    result.shadingIrradiance = Visibility == kProbeVisibilityNone ?
        SampleIrradianceProbes(ProbesIrradiance, LinearClamp, Grid, result.position, result.shadingNormal) :
        SampleVisibleIrradianceProbes(Visibility, ProbesIrradiance, distanceAtlas, LinearClamp, Grid, result.position, result.geometryNormal,
            result.shadingNormal);
    Contribution[0] = result;
}
