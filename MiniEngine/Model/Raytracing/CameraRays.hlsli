#ifndef __CAMERA_RAYS_HLSLI__
#define __CAMERA_RAYS_HLSLI__

// CameraRays.h의 CameraRays와 같은 layout이다.
struct CameraRays
{
    float3 origin;
    float3 viewportCornerDirection;
    float3 pixelStepRight;
    float3 pixelStepDown;
    float2 viewportOffset;
    // DXC는 cbuffer에서 struct 뒤의 값을 struct의 남은 자리에 채우므로, C++의 CameraRays처럼 마지막 16 byte를 채운다.
    float2 padding;
};

// viewport 내의 특정 위치를 지나가는 ray.
RayDesc GetViewportRay(CameraRays rays, float2 viewportPosition)
{
    RayDesc ray;
    ray.Origin = rays.origin;
    ray.Direction = normalize(rays.viewportCornerDirection + viewportPosition.x * rays.pixelStepRight + viewportPosition.y * rays.pixelStepDown);
    ray.TMin = 0.0;
    ray.TMax = 1e30;
    return ray;
}

// ray를 raster가 그리는 near, far plane 사이로 자른다. 그래야 raster가 볼 수 있는 것만 맞는다.
void ClipToRasterDepthRange(inout RayDesc ray, float3 cameraForward, float nearZ, float farZ)
{
    const float cosine = dot(ray.Direction, cameraForward);
    ray.TMin = nearZ / cosine;
    ray.TMax = farZ / cosine;
}

// Camera.cpp의 reverse Z projection을 거꾸로 풀어, depth buffer 값을 camera의 forward 방향으로 잰 거리로 되돌린다.
float GetRasterViewDepth(float rasterZ, float nearZ, float farZ)
{
    return nearZ * farZ / (nearZ + rasterZ * (farZ - nearZ));
}

#endif
