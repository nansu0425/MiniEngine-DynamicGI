#ifndef __CAMERA_RAYS_HLSLI__
#define __CAMERA_RAYS_HLSLI__

// CameraRays.h의 CameraRays와 같은 layout이다.
struct CameraRays
{
    float3 origin;
    float3 topLeftPixelDirection;
    float3 pixelStepRight;
    float3 pixelStepDown;
    // DXC는 cbuffer에서 struct 뒤의 값을 struct의 남은 자리에 채우므로, C++의 Vector3처럼 마지막 16 byte를 채운다.
    float padding;
};

// raster가 pixel의 coverage와 depth를 정한 sample 위치를 지나는 ray. TMax는 끝이 없으므로 near, far plane이 필요한 query는 TMin, TMax를 바꾼다.
RayDesc GetCameraRay(CameraRays rays, uint2 pixel)
{
    RayDesc ray;
    ray.Origin = rays.origin;
    ray.Direction = normalize(rays.topLeftPixelDirection + pixel.x * rays.pixelStepRight + pixel.y * rays.pixelStepDown);
    ray.TMin = 0.0;
    ray.TMax = 1e30;
    return ray;
}

#endif
