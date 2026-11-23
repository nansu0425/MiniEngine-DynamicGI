#include "Model/DynamicGI/ProbeRayHit.hlsli"
#include "Model/DynamicGI/ProbeGuide.hlsli"
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
Texture2D<float2> ProbesGuide : register(t5);
SamplerComparisonState ShadowMapSampler : register(s0);
SamplerState LinearClamp : register(s1);
RWTexture2D<float4> RaySamples : register(u0);
RWTexture2D<float> RayBackFaces : register(u1);
RWTexture2D<float4> WeightedRayDirections : register(u2);

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

float3 GetProbeRayDirection(uint probe, uint ray)
{
    if (ray < Rays.uniformRaysPerProbe)
        return GetUniformProbeRayDirection(Rays, ray);
    return SampleProbeGuideDirection(ProbesGuide, Rays, probe, ray - Rays.uniformRaysPerProbe);
}

float GetProbeRayWeight(uint probe, float3 direction)
{
    if (Rays.guidedRaysPerProbe == 0)
        return 1.0;

    static const float kSphereSolidAngle = 4.0 * kPi;
    // 1 solid angle에 uniform ray가 몇 개?
    const float uniformRayDensity = Rays.uniformRaysPerProbe / kSphereSolidAngle;
    // 1 solid angle에 guided ray가 몇 개?
    const float guidedRayDensity = Rays.guidedRaysPerProbe * GetProbeGuideProbabilityDensity(ProbesGuide, probe, direction);
    
    // uniform ray 하나가 원래 담당하던 넓이에서 현재 ray가 차지하는 비율
    return uniformRayDensity / (uniformRayDensity + guidedRayDensity);
}

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    const uint ray = DTid.x;
    const uint probe = DTid.y;
    if (ray >= GetProbeRayCount(Rays) || probe >= GetProbeCount(Rays.grid))
        return;

    const ProbePositions positions = MakeProbePositions(Rays.grid, ProbesOffset);
    const float3 origin = GetProbePosition(positions, GetProbeCoordinate(Rays.grid, probe));
    const float3 direction = GetProbeRayDirection(probe, ray);
    const ProbeRaySample sample = TraceProbeRay(positions, origin, direction);
    WeightedRayDirections[DTid] = float4(direction, GetProbeRayWeight(probe, direction));
    RaySamples[DTid] = float4(sample.radiance, sample.distance);
    RayBackFaces[DTid] = sample.hitsBackFace ? 1.0 : 0.0;
}
