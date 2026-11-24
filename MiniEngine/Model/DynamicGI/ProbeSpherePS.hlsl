#include "Model/DynamicGI/ProbeSphere.hlsli"
#include "Model/DynamicGI/ProbeClassification.hlsli"
#include "Model/DynamicGI/ProbeGuide.hlsli"

static const float kInactiveRingCosine = 0.35;
static const float3 kGuideTexelBorderColor = float3(0.0, 0.0, 0.0);
static const float3 kHighlightedGuideTexelBorderColor = float3(1.0, 0.55, 0.1);
static const float kMaxGuideTexelBorderWidth = 0.25;

Texture2D<float4> ProbesIrradiance : register(t0);
Texture2D<float> ProbesBackFaceRatio : register(t1);
SamplerState LinearClamp : register(s0);

struct ProbeQuadPixel
{
    float4 color : SV_Target0;
    float depth : SV_DepthLessEqual;
};

bool IsOnGuideTexelBorder(float3 normal)
{
    const float2 texelPosition = GetProbeGuideOctahedralTexelPosition(normal);
    const float2 distanceToBorder = abs(frac(texelPosition + 0.5) - 0.5);
    const float2 borderWidth = min(fwidth(texelPosition), kMaxGuideTexelBorderWidth);
    return any(distanceToBorder < borderWidth);
}

bool IsHighlightedGuideTexel(uint3 probeCoordinate, float3 normal)
{
    return GetProbeIndex(Grid, probeCoordinate) == HighlightedProbe && GetProbeGuideTexelIndex(normal) == HighlightedGuideTexel;
}

float3 DrawGuideTexelBorder(float3 color, uint3 probeCoordinate, float3 normal)
{
    if (ShowGuideTexelBorders == 0 || !IsOnGuideTexelBorder(normal))
        return color;
    return IsHighlightedGuideTexel(probeCoordinate, normal) ? kHighlightedGuideTexelBorderColor : kGuideTexelBorderColor;
}

ProbeQuadPixel main(ProbeQuadVertex vertex)
{
    // camera ray와 probe sphere의 교점 방정식을 세운다
    const float3 direction = normalize(vertex.worldPosition - CameraPosition);
    const float3 fromCenter = CameraPosition - vertex.probePosition;
    const float halfB = dot(fromCenter, direction);
    const float discriminant = halfB * halfB - (dot(fromCenter, fromCenter) - Radius * Radius);
    
    // ray가 sphere와 교차하지 않는 pixel은 버린다
    if (discriminant < 0.0)
        discard;

    // sphere 앞쪽 교점과 normal을 구한다
    const float3 hit = CameraPosition + (-halfB - sqrt(discriminant)) * direction;
    const float3 normal = (hit - vertex.probePosition) / Radius;
    const float4 clipPosition = mul(ViewProj, float4(hit, 1.0));
    
    // probe의 normal 방향 irradiance를 구하고, quad 뒤에 있는 교점의 depth를 구한다
    ProbeQuadPixel pixel;
    const bool inactive = GetProbeActiveWeight(Classification, MaxBackFaceRatio, ProbesBackFaceRatio, Grid, vertex.probeCoordinate) == 0.0;
    const float3 irradiance = SampleProbeIrradiance(ProbesIrradiance, LinearClamp, Grid, vertex.probeCoordinate, normal);
    pixel.color = inactive && dot(normal, -direction) < kInactiveRingCosine ?
        float4(1.0, 0.0, 0.0, 1.0) : float4(DrawGuideTexelBorder(irradiance, vertex.probeCoordinate, normal), 1.0);
    pixel.depth = min(clipPosition.z / clipPosition.w, vertex.position.z);
    
    return pixel;
}
