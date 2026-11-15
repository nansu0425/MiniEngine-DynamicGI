#ifndef __PROBE_RAY_HIT_HLSLI__
#define __PROBE_RAY_HIT_HLSLI__

#include "Model/DynamicGI/ProbeRays.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"

bool TraceProbeRayHit(IrradianceProbeGrid grid, float3 origin, float3 direction, out RayHit rayHit, out float distance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = 1e30;

    const float maxDistance = GetProbeRayMaxDistance(grid);
    const bool hit = TraceClosestHit(ray, RAY_FLAG_NONE, rayHit);
    distance = hit ? min(rayHit.t, maxDistance) : maxDistance;
    return hit;
}

#endif
