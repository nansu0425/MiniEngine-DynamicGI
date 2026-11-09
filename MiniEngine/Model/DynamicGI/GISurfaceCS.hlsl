#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"
#include "Model/DynamicGI/GISurface.hlsli"

cbuffer Constants : register(b0)
{
    CameraRays Rays;
    uint Width;
    uint Height;
};

RWStructuredBuffer<GISurface> Surfaces : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height)
        return;

    GISurface surface = (GISurface)0;
    const RayDesc ray = GetViewportRay(Rays, float2(DTid) + 0.5 - Rays.viewportOffset);
    
    RayHit hit;
    if (TraceClosestHit(ray, kCullSingleSidedBackFaces, hit))
    {
        const HitSurface hitSurface = GetHitSurface(hit, kSharpestTextureLod);
        surface.position = hitSurface.position;
        surface.hit = 1;
        surface.geometryNormal = hitSurface.geometryNormal;
        surface.roughness = hitSurface.roughness;
        surface.shadingNormal = hitSurface.shadingNormal;
        surface.metallic = hitSurface.metallic;
        surface.view = -ray.Direction;
        surface.baseColor = hitSurface.baseColor.rgb;
    }
    
    Surfaces[DTid.y * Width + DTid.x] = surface;
}
