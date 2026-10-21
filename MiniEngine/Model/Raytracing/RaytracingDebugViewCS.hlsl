#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"
#include "Model/Raytracing/SurfaceRayOrigin.hlsli"
#include "Core/Shaders/ShaderUtility.hlsli"

// RaytracingDebugView.cpp의 View와 같은 값이다.
static const uint kHitT = 1;
static const uint kInstance = 2;
static const uint kFrontFace = 3;
static const uint kGeometryNormal = 4;
static const uint kShadingNormal = 5;
static const uint kUV0 = 6;
static const uint kBaseColor = 7;
static const uint kMetallic = 8;
static const uint kRoughness = 9;
static const uint kEmissive = 10;
static const uint kSunVisibility = 11;
static const uint kSunCullingDifference = 12;

cbuffer Constants : register(b0)
{
    CameraRays Rays;
    uint View;
    float NearZ;
    float FarZ;
    uint CullSingleSidedBackFaces;
    float3 SunDirection;
};

RWTexture2D<float4> Output : register(u0);

float3 Hue(float hue)
{
    return saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
}

// 가까울수록 밝다. camera의 near부터 far까지를 log scale로 나눠 scene 크기가 달라도 명암이 남게 한다.
float3 HitTColor(float t)
{
    return 1.0 - saturate(log(t / NearZ) / log(FarZ / NearZ));
}

// 번호가 이웃한 instance끼리 hue가 멀어지도록 golden ratio만큼 hue를 돌린다.
float3 InstanceColor(uint instanceIndex)
{
    static const float kGoldenRatioFraction = 0.618034;
    return Hue(frac(instanceIndex * kGoldenRatioFraction));
}

float3 FrontFaceColor(bool frontFace)
{
    return frontFace ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
}

float3 NormalColor(float3 normal)
{
    return normal * 0.5 + 0.5;
}

// scene overlay는 display에 맞게 encode된 화면 위에 섞이므로, linear color는 display처럼 encode해야 texture대로 보인다.
float3 LinearColor(float3 color)
{
    return ApplyDisplayProfile(color, DISPLAY_PLANE_FORMAT);
}

float3 SurfaceColor(HitSurface surface)
{
    if (View == kGeometryNormal)
        return NormalColor(surface.geometryNormal);
    if (View == kShadingNormal)
        return NormalColor(surface.shadingNormal);
    if (View == kUV0)
        return float3(frac(surface.uv0), 0.0);
    if (View == kBaseColor)
        return LinearColor(surface.baseColor.rgb);
    if (View == kMetallic)
        return surface.metallic;
    if (View == kRoughness)
        return surface.roughness;
    if (View == kEmissive)
        return LinearColor(surface.emissive);
    return 0.0;
}

// Cull Single-Sided Back Faces를 끄면 single-sided material의 뒷면에도 ray가 맞는다.
uint DebugViewRayFlags()
{
    return CullSingleSidedBackFaces != 0 ? kCullSingleSidedBackFaces : RAY_FLAG_NONE;
}

// 면이 sun을 등지면 그 면이 sun을 가리므로 ray를 쏠 필요가 없어 false를 돌려준다.
bool DescribeSunRay(HitSurface surface, out RayDesc ray)
{
    ray.Origin = SurfaceRayOrigin(surface.position, surface.geometryNormal);
    ray.Direction = SunDirection;
    ray.TMin = 0.0;
    ray.TMax = 1e30;
    return dot(surface.geometryNormal, SunDirection) > 0.0;
}

// sun이 보이면 흰색, 가려지면 검정이다.
float3 SunVisibilityColor(RayHit hit)
{
    RayDesc ray;
    if (!DescribeSunRay(GetHitSurface(hit), ray))
        return 0.0;
    return IsOccluded(ray, DebugViewRayFlags()) ? 0.0 : 1.0;
}

// sun을 향한 ray가 single-sided material의 뒷면을 cull할 때와 하지 않을 때 결과가 다른 곳을 빨강으로 칠한다.
// 빨강은 ray가 single-sided 면을 뒤에서 만나 지나가고, 그 뒤에 sun을 가리는 다른 면이 없는 곳이다.
// 나머지는 Sun Visibility와 같다.
float3 SunCullingDifferenceColor(RayHit hit)
{
    RayDesc ray;
    if (!DescribeSunRay(GetHitSurface(hit), ray))
        return 0.0;

    const bool occludedWithCulling = IsOccluded(ray, kCullSingleSidedBackFaces);
    if (occludedWithCulling != IsOccluded(ray, RAY_FLAG_NONE))
        return float3(1.0, 0.0, 0.0);
    return occludedWithCulling ? 0.0 : 1.0;
}

float3 HitColor(RayHit hit)
{
    if (View == kHitT)
        return HitTColor(hit.t);
    if (View == kInstance)
        return InstanceColor(hit.instanceIndex);
    if (View == kFrontFace)
        return FrontFaceColor(hit.frontFace);
    if (View == kSunVisibility)
        return SunVisibilityColor(hit);
    if (View == kSunCullingDifference)
        return SunCullingDifferenceColor(hit);
    return SurfaceColor(GetHitSurface(hit));
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    float3 color = 0.0;
    RayHit hit;
    if (TraceClosestHit(GetCameraRay(Rays, pixel), DebugViewRayFlags(), hit))
        color = HitColor(hit);
    Output[pixel] = float4(color, 1.0);
}
