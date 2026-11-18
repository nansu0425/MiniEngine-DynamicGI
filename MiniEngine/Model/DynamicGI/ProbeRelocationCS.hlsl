#include "Model/DynamicGI/ProbeRayHit.hlsli"
#include "Model/DynamicGI/ProbeClassification.hlsli"
#include "Model/DynamicGI/ProbeRelocation.hlsli"

cbuffer Constants : register(b0)
{
    float MaxBackFaceRatio;
    float MaxOffset;
    float MaxPushScale;
    float MinPushScale;
    float MinFrontFaceDistance;
    ProbeRays Rays;
};

RWTexture2D<float4> ProbesOffset : register(u0);

static const uint kNumPushScales = 100;

struct NearbyFaces
{
    float backFaceRatio;
    float3 closestBackFace;
    float closestFrontFaceDistance;
    float3 closestFrontFaceDirection;
};

NearbyFaces FindNearbyFaces(float3 origin)
{
    uint backFaceCount = 0;
    float closestBackFaceDistance = 1e30;
    NearbyFaces faces;
    faces.closestBackFace = 0.0;
    faces.closestFrontFaceDistance = 1e30;
    faces.closestFrontFaceDirection = 0.0;
    for (uint ray = 0; ray < Rays.raysPerProbe; ++ray)
    {
        const float3 direction = GetProbeRayDirection(Rays, ray);
        RayHit rayHit;
        float distance;
        if (!TraceProbeRayHit(Rays.grid, origin, direction, rayHit, distance))
            continue;
        if (HitsSingleSidedBackFace(rayHit))
        {
            ++backFaceCount;
            if (rayHit.t < closestBackFaceDistance)
            {
                closestBackFaceDistance = rayHit.t;
                faces.closestBackFace = direction * rayHit.t;
            }
        }
        else if (rayHit.t < faces.closestFrontFaceDistance)
        {
            faces.closestFrontFaceDistance = rayHit.t;
            faces.closestFrontFaceDirection = direction;
        }
    }
    faces.backFaceRatio = (float)backFaceCount / Rays.raysPerProbe;
    return faces;
}

bool IsWithinMaxOffset(float3 offset)
{
    return all(abs(offset) < MaxOffset);
}

float4 PushThroughClosestBackFace(float3 offset, float3 closestBackFace)
{
    for (uint step = 0; step < kNumPushScales; ++step)
    {
        const float scale = lerp(MaxPushScale, MinPushScale, (float)step / (kNumPushScales - 1));
        const float3 pushed = offset + closestBackFace * scale;
        if (IsWithinMaxOffset(pushed))
            return float4(pushed, kProbeRelocationBackFacePush);
    }
    return float4(offset, kProbeRelocationBlocked);
}

float4 KeepAwayFromClosestFrontFace(float3 position, float3 offset, NearbyFaces faces)
{
    const float3 away = -faces.closestFrontFaceDirection;
    RayHit rayHit;
    float freeDistance;
    TraceProbeRayHit(Rays.grid, position, away, rayHit, freeDistance);
    const float pushDistance = min(MinFrontFaceDistance - faces.closestFrontFaceDistance, (freeDistance - faces.closestFrontFaceDistance) * 0.5);
    const float3 pushed = offset + away * pushDistance;
    if (pushDistance > 0.0 && IsWithinMaxOffset(pushed))
        return float4(pushed, kProbeRelocationFrontFacePush);
    return float4(offset, kProbeRelocationBlocked);
}

float4 RelocateProbe(float3 gridPosition, float3 offset)
{
    const float3 position = gridPosition + offset;
    const NearbyFaces faces = FindNearbyFaces(position);
    if (IsProbeInsideGeometry(MaxBackFaceRatio, faces.backFaceRatio))
        return PushThroughClosestBackFace(offset, faces.closestBackFace);
    if (faces.closestFrontFaceDistance < MinFrontFaceDistance)
        return KeepAwayFromClosestFrontFace(position, offset, faces);
    return float4(offset, kProbeRelocationNotMoved);
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint probe = DTid.x;
    if (probe >= GetProbeCount(Rays.grid))
        return;

    const uint2 texel = uint2(probe, 0);
    ProbesOffset[texel] = RelocateProbe(GetProbeGridPosition(Rays.grid, GetProbeCoordinate(Rays.grid, probe)), ProbesOffset[texel].xyz);
}
