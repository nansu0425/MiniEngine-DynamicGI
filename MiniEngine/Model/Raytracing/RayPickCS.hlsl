#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"

cbuffer Constants : register(b0)
{
    CameraRays Rays;
    // raster를 그린 camera
    float3 CameraForward;
    float NearZ;
    float FarZ;
    uint NumPixels;
};

// raster가 그린 depth buffer
Texture2D<float> RasterDepth : register(t0);
// pick할 pixel의 scene 좌표
StructuredBuffer<uint2> Pixels : register(t1);

// RayPick.cpp의 Result와 같은 layout이다.
struct Result
{
    uint hit;
    // ray를 따라 잰 거리
    float distance;
    // camera의 forward 방향으로 잰 거리. raster depth와 같은 축이다.
    float depth;
    float3 position;
    // triangle 면의 normal. ray가 온 쪽을 향한다.
    float3 normal;
    // HitSurface의 vertexNormal과 shadingNormal
    float3 vertexNormal;
    float3 shadingNormal;
    uint frontFace;
    // alpha test가 ray가 맞은 것을 정했는지. 그런 pixel은 raster와 다른 것을 맞을 수 있다.
    uint alphaTest;
    uint instance;
    uint geometry;
    uint primitive;
    uint material;
    float4 baseColor;
    float rasterZ;
    float rasterDepth;
};

RWStructuredBuffer<Result> Results : register(u0);

[numthreads(64, 1, 1)]
void main(uint index : SV_DispatchThreadID)
{
    if (index >= NumPixels)
        return;

    const uint2 pixel = Pixels[index];
    Result result = (Result)0;
    result.rasterZ = RasterDepth[pixel];
    result.rasterDepth = GetRasterViewDepth(result.rasterZ, NearZ, FarZ);

    // raster가 보는 것과 견주도록 raster의 sample 위치를 지나 raster의 depth 범위 안에서, raster처럼 single-sided 면의 뒷면을 cull한다.
    RayDesc ray = GetRasterSampleRay(Rays, pixel);
    ClipToRasterDepthRange(ray, CameraForward, NearZ, FarZ);
    RayHit hit;
    const bool rayHit = TraceClosestHit(ray, kCullSingleSidedBackFaces, hit);
    result.alphaTest = IsDecidedByAlphaTest(ray, kCullSingleSidedBackFaces, rayHit, hit);
    if (rayHit)
    {
        const HitSurface surface = GetHitSurface(hit, kSharpestTextureLod);
        result.hit = 1;
        result.distance = hit.t;
        result.depth = hit.t * dot(ray.Direction, CameraForward);
        result.position = surface.position;
        result.normal = surface.geometryNormal;
        result.vertexNormal = surface.vertexNormal;
        result.shadingNormal = surface.shadingNormal;
        result.frontFace = hit.frontFace;
        result.instance = hit.instanceIndex;
        result.geometry = hit.geometryIndex;
        result.primitive = hit.primitiveIndex;
        result.material = GetHitTriangle(hit.instanceIndex, hit.geometryIndex, hit.primitiveIndex, hit.barycentrics).geometry.material;
        result.baseColor = surface.baseColor;
    }
    Results[index] = result;
}
