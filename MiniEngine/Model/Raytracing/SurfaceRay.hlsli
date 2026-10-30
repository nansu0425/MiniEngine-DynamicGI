#ifndef __SURFACE_RAY_HLSLI__
#define __SURFACE_RAY_HLSLI__

#include "Model/Raytracing/SurfaceRayOrigin.hlsli"

// surface에서 direction으로 나가는 ray. docs/policy.md의 "camera 밖에서 본 doubleSided"에 따라 면이 geometric normal로 그 방향을 등지면
// 그 면이 가리므로, ray를 쏠 필요가 없어 false를 돌려준다.
bool DescribeSurfaceRay(float3 position, float3 geometryNormal, float3 direction, out RayDesc ray)
{
    ray.Origin = SurfaceRayOrigin(position, geometryNormal);
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = 1e30;
    return dot(geometryNormal, direction) > 0.0;
}

#endif
