#include "Model/Renderer/GBufferTextures.hlsli"
#include "Model/Renderer/SurfaceRadiance.hlsli"
#include "Core/Shaders/IrradianceProbes.hlsli"

cbuffer Constants : register(b0)
{
    float4x4 ShadowMatrix;
    float3 ViewerPos;
    uint Width;
    float3 LightDirection;
    uint Height;
    float3 LightIlluminance;
    uint UseIrradianceProbes;
    IrradianceProbeGrid Probes;
}

Texture2D<float> ShadowMap : register(t5);
Texture2D<float3> IndirectRadiance : register(t6);
Texture2D<float4> ProbeIrradiance : register(t7);
RWTexture2D<float3> SceneColor : register(u0);
SamplerComparisonState ShadowMapSampler : register(s0);
SamplerState LinearClamp : register(s1);

float3 GetIndirectRadiance(RasterSurface surface, float3 view, uint2 pixel)
{
    if (UseIrradianceProbes == 0)
        return IndirectRadiance[pixel];

    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, view);
    return kPi * EvaluateGltfDiffuseLobeForIrradiance(brdf) * SampleIrradianceProbes(ProbeIrradiance, LinearClamp, Probes, surface.position, surface.shadingNormal);
}

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
        LightDirection, LightIlluminance, GetIndirectRadiance(surface, view, DTid.xy));
}
