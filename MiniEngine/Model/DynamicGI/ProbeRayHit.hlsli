#ifndef __PROBE_RAY_HIT_HLSLI__
#define __PROBE_RAY_HIT_HLSLI__

#include "Model/DynamicGI/ProbeRays.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"

bool TraceUnclampedProbeRayHit(float3 origin, float3 direction, out RayHit rayHit, out float hitDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = kProbeRayMissDistance;

    const bool hit = TraceClosestHit(ray, RAY_FLAG_NONE, rayHit);
    hitDistance = hit ? rayHit.t : kProbeRayMissDistance;
    return hit;
}

bool TraceProbeRayHit(IrradianceProbeGrid grid, float3 origin, float3 direction, out RayHit rayHit, out float distance)
{
    float hitDistance;
    const bool hit = TraceUnclampedProbeRayHit(origin, direction, rayHit, hitDistance);
    distance = GetProbeRayKeptDistance(grid, hitDistance);
    return hit;
}

#endif
