#include "Model/Raytracing/HitSurface.hlsli"

cbuffer Constants : register(b0)
{
    float4x4 InvViewProj;
    float3 CameraPos;
    float2 ViewportOffset;
    float2 ViewportSize;
};

RWTexture2D<float4> IndirectIrradiance : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    // raster가 sample하는 지점의 pixel 좌표
    float2 pixel = float2(DTid.xy) + 0.5;
    pixel -= ViewportOffset;
    
    // pixel -> ndc
    float2 ndc = pixel / ViewportSize * 2 - 1;
    ndc.y = -ndc.y;
    
    // ndc -> world
    float4 h = mul(InvViewProj, float4(ndc.xy, 1.0, 1.0));
    float3 nearW = h.xyz / h.w;

    // pixel을 지나는 primary ray 생성
    RayDesc ray;
    ray.Origin = nearW;
    ray.Direction = normalize(nearW - CameraPos);
    ray.TMin = 0.0;
    ray.TMax = 1e30;
    
    // TLAS에 ray를 쏜다
    RayHit hit;
    if (TraceClosestHit(ray, kCullSingleSidedBackFaces, hit))
    {
        const HitSurface surface = GetHitSurface(hit, kSharpestTextureLod);
        IndirectIrradiance[DTid] = surface.baseColor; // 테스트 목적으로 baseColor 기록
    }
}
