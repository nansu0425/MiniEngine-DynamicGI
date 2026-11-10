#include "Model/DynamicGI/ProbeRays.hlsli"
#include "Model/DynamicGI/HitLighting.hlsli"

cbuffer Constants : register(b0)
{
    GILights Lights;
    ProbeRays Rays;
};

Texture2D<float> ShadowMap : register(t0);
Texture2D<float4> ProbeIrradiance : register(t1);
SamplerComparisonState ShadowMapSampler : register(s0);
SamplerState LinearClamp : register(s1);
RWTexture2D<float4> RayRadiance : register(u0);

float3 TraceProbeRay(float3 origin, float3 direction)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = 1e30;

    RayHit rayHit;
    if (!TraceClosestHit(ray, kCullSingleSidedBackFaces, rayHit))
        return Lights.skyRadiance;

    const HitSurface surface = GetHitSurface(rayHit, kSharpestTextureLod);
    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, -direction);
    const float3 indirectRadiance = kPi * EvaluateGltfDiffuseLobeForIrradiance(brdf) *
        SampleIrradianceProbes(ProbeIrradiance, LinearClamp, Rays.grid, surface.position, surface.shadingNormal);
    const float3 directRadiance = GetHitDirectRadiance(surface, brdf, Lights, ShadowMap, ShadowMapSampler);

    return surface.emissive + directRadiance + indirectRadiance;
}

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    const uint ray = DTid.x;
    const uint probe = DTid.y;
    if (ray >= Rays.raysPerProbe || probe >= GetProbeCount(Rays.grid))
        return;

    const float3 origin = GetProbePosition(Rays.grid, GetProbeCoordinate(Rays.grid, probe));
    RayRadiance[DTid] = float4(TraceProbeRay(origin, GetProbeRayDirection(Rays, ray)), 0.0);
}
