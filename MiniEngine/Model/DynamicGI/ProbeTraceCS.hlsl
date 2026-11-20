#include "Model/DynamicGI/ProbeRayHit.hlsli"
#include "Model/DynamicGI/ProbeWeighting.hlsli"
#include "Model/DynamicGI/HitLighting.hlsli"
#include "Model/DynamicGI/ProbeEmissiveSamples.hlsli"

cbuffer Constants : register(b0)
{
    uint DistanceTexelsPerSide;
    float BackFaceDistanceScale;
    ProbeWeighting Weighting;
    ProbeEmissiveSampling EmissiveSampling;
    GILights Lights;
    ProbeRays Rays;
};

Texture2D<float> ShadowMap : register(t0);
Texture2D<float4> ProbesIrradiance : register(t1);
Texture2D<float2> ProbesDistance : register(t2);
Texture2D<float> ProbesBackFaceRatio : register(t3);
Texture2D<float4> ProbesOffset : register(t4);
SamplerComparisonState ShadowMapSampler : register(s0);
SamplerState LinearClamp : register(s1);
RWTexture2D<float4> RaySamples : register(u0);
RWTexture2D<float> RayBackFaces : register(u1);

struct ProbeRaySample
{
    float3 radiance;
    float distance;
    bool hitsBackFace;
};

ProbeRaySample TraceProbeRay(ProbePositions positions, float3 origin, float3 direction)
{
    ProbeRaySample sample;
    sample.hitsBackFace = false;
    RayHit rayHit;
    if (!TraceProbeRayHit(Rays.grid, origin, direction, rayHit, sample.distance))
    {
        sample.radiance = Lights.skyRadiance;
        return sample;
    }
    if (HitsSingleSidedBackFace(rayHit))
    {
        sample.hitsBackFace = true;
        sample.radiance = 0.0;
        sample.distance *= BackFaceDistanceScale;
        return sample;
    }

    const HitSurface surface = GetHitSurface(rayHit, kSharpestTextureLod);
    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, -direction);
    const ProbeDistanceAtlas distanceAtlas = MakeProbeDistanceAtlas(ProbesDistance, DistanceTexelsPerSide);
    const ProbeVolume volume = MakeProbeVolume(positions, ProbesIrradiance, distanceAtlas, ProbesBackFaceRatio);
    const ProbeLookupSurface lookupSurface = MakeProbeLookupSurface(surface.position, surface.geometryNormal, surface.shadingNormal);
    const float3 irradiance = InterpolatesTrilinearly(Weighting) ?
        SampleIrradianceProbes(ProbesIrradiance, LinearClamp, Rays.grid, surface.position, surface.shadingNormal) :
        SampleWeightedIrradianceProbes(Weighting, volume, LinearClamp, lookupSurface);
    const float3 indirectRadiance = kPi * EvaluateGltfDiffuseLobeForIrradiance(brdf) * irradiance;
    const float3 directRadiance = GetHitDirectRadiance(surface, brdf, Lights, ShadowMap, ShadowMapSampler);

    const float3 emissiveRadiance = HasEmissiveSamples(EmissiveSampling) ? 0.0 : surface.emissive;

    sample.radiance = emissiveRadiance + directRadiance + indirectRadiance;
    return sample;
}

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    const uint ray = DTid.x;
    const uint probe = DTid.y;
    if (ray >= Rays.raysPerProbe || probe >= GetProbeCount(Rays.grid))
        return;

    const ProbePositions positions = MakeProbePositions(Rays.grid, ProbesOffset);
    const float3 origin = GetProbePosition(positions, GetProbeCoordinate(Rays.grid, probe));
    const ProbeRaySample sample = TraceProbeRay(positions, origin, GetProbeRayDirection(Rays, ray));
    RaySamples[DTid] = float4(sample.radiance, sample.distance);
    RayBackFaces[DTid] = sample.hitsBackFace ? 1.0 : 0.0;
}
