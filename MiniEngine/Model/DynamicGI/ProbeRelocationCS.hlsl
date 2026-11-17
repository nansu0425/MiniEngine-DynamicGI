#include "Model/DynamicGI/ProbeRayHit.hlsli"
#include "Model/DynamicGI/ProbeClassification.hlsli"
#include "Model/DynamicGI/ProbeRelocation.hlsli"

cbuffer Constants : register(b0)
{
    float MaxBackFaceRatio;
    float MaxOffset;
    float MaxPushScale;
    float MinPushScale;
    ProbeRays Rays;
};

RWTexture2D<float4> ProbesOffset : register(u0);

static const uint kNumPushScales = 100;

struct BackFaceHits
{
    float ratio;
    float3 closestVector;
};

BackFaceHits TraceBackFaceHits(float3 origin)
{
    uint count = 0;
    float closestDistance = 1e30;
    BackFaceHits hits;
    hits.closestVector = 0.0;
    for (uint ray = 0; ray < Rays.raysPerProbe; ++ray)
    {
        const float3 direction = GetProbeRayDirection(Rays, ray);
        RayHit rayHit;
        float distance;
        if (!TraceProbeRayHit(Rays.grid, origin, direction, rayHit, distance) || !HitsSingleSidedBackFace(rayHit))
            continue;
        ++count;
        if (rayHit.t < closestDistance)
        {
            closestDistance = rayHit.t;
            hits.closestVector = direction * rayHit.t;
        }
    }
    hits.ratio = (float)count / Rays.raysPerProbe;
    return hits;
}

float4 PushThroughClosestBackFace(float3 offset, float3 closestBackFace)
{
    for (uint step = 0; step < kNumPushScales; ++step)
    {
        const float scale = lerp(MaxPushScale, MinPushScale, (float)step / (kNumPushScales - 1));
        const float3 pushed = offset + closestBackFace * scale;
        if (all(abs(pushed) < MaxOffset))
            return float4(pushed, kProbeRelocationBackFacePush);
    }
    return float4(offset, kProbeRelocationBlocked);
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint probe = DTid.x;
    if (probe >= GetProbeCount(Rays.grid))
        return;

    const uint2 texel = uint2(probe, 0);
    const float3 offset = ProbesOffset[texel].xyz;
    const BackFaceHits hits = TraceBackFaceHits(GetProbeGridPosition(Rays.grid, GetProbeCoordinate(Rays.grid, probe)) + offset);
    ProbesOffset[texel] = IsProbeInsideGeometry(MaxBackFaceRatio, hits.ratio) ?
        PushThroughClosestBackFace(offset, hits.closestVector) : float4(offset, kProbeRelocationNotMoved);
}
