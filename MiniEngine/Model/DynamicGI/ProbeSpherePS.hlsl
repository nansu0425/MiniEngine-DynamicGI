#include "Model/DynamicGI/ProbeSphere.hlsli"

Texture2D<float4> ProbeIrradiance : register(t0);
SamplerState LinearClamp : register(s0);

struct ProbeQuadPixel
{
    float4 color : SV_Target0;
    float depth : SV_DepthLessEqual;
};

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
    pixel.color = float4(SampleProbeIrradiance(ProbeIrradiance, LinearClamp, Grid, vertex.probeCoordinate, normal), 1.0);
    pixel.depth = min(clipPosition.z / clipPosition.w, vertex.position.z);
    
    return pixel;
}
