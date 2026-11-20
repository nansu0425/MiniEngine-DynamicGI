#include "Model/DynamicGI/ProbeEmissiveSamples.hlsli"
#include "Model/DynamicGI/ProbePositions.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"

cbuffer Constants : register(b0)
{
    ProbeEmissiveSampling EmissiveSampling;
    IrradianceProbeGrid Grid;
};

Texture2D<float4> ProbesOffset : register(t0);
StructuredBuffer<EmissiveTriangle> EmissiveTriangles : register(t1);
RWTexture2D<float4> EmissiveSamples : register(u0);

static const float kEmissiveRayOvershoot = 1.01;

float4 TraceEmissiveSample(float3 origin, EmissivePoint emissivePoint)
{
    const float3 toEmissivePoint = emissivePoint.position - origin;
    const float distance = length(toEmissivePoint);
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = toEmissivePoint / distance;
    ray.TMin = 0.0;
    ray.TMax = distance * kEmissiveRayOvershoot;

    RayHit hit;
    if (!TraceClosestHit(ray, RAY_FLAG_NONE, hit) || HitsSingleSidedBackFace(hit) || !HitsEmissiveTriangle(hit, emissivePoint.source))
        return float4(0.0, 0.0, 0.0, distance);

    const HitSurface surface = GetHitSurface(hit, kSharpestTextureLod);
    const float cosine = max(0.0, dot(surface.geometryNormal, -ray.Direction));
    return float4(surface.emissive * cosine / (distance * distance * emissivePoint.areaPdf), distance);
}

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    const uint sample = DTid.x;
    const uint probe = DTid.y;
    if (sample >= EmissiveSampling.samplesPerProbe || probe >= GetProbeCount(Grid))
        return;

    const ProbePositions positions = MakeProbePositions(Grid, ProbesOffset);
    const float3 origin = GetProbePosition(positions, GetProbeCoordinate(Grid, probe));
    const EmissivePoint emissivePoint = SampleProbeEmissivePoint(EmissiveSampling, EmissiveTriangles, probe, sample);
    EmissiveSamples[DTid] = TraceEmissiveSample(origin, emissivePoint);
}
