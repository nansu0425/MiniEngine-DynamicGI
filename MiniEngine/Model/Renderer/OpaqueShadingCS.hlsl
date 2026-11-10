#include "Model/Renderer/GBufferTextures.hlsli"
#include "Model/Renderer/SurfaceRadiance.hlsli"

cbuffer Constants : register(b0)
{
    float4x4 ShadowMatrix;
    float3 ViewerPos;
    uint Width;
    float3 LightDirection;
    uint Height;
    float3 LightIlluminance;
}

Texture2D<float> ShadowMap : register(t5);
Texture2D<float3> IndirectDiffuseRadiance : register(t6);
RWTexture2D<float3> SceneColor : register(u0);
SamplerComparisonState ShadowMapSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height)
        return;

    RasterSurface surface;
    if (!LoadGBuffer(DTid.xy, surface))
        return;

    const float3 view = normalize(ViewerPos - surface.position);
    const float3 shadowCoord = mul(ShadowMatrix, float4(surface.position, 1.0)).xyz;
    SceneColor[DTid.xy] = GetOutgoingRadiance(surface, view, shadowCoord, ShadowMap, ShadowMapSampler,
        LightDirection, LightIlluminance, IndirectDiffuseRadiance[DTid.xy]);
}
