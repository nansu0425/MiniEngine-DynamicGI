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
    uint IndirectIrradianceSource;
    uint ShadingOutput;
    IrradianceProbeGrid Probes;
}

static const uint kShadingOutputLit = 0;
static const uint kShadingOutputIndirectIrradiance = 1;
static const uint kShadingOutputIndirectRadiance = 2;

static const uint kIndirectIrradianceNone = 0;
static const uint kProbesIndirectIrradiance = 1;
static const uint kScreenIndirectIrradiance = 2;

Texture2D<float> ShadowMap : register(t5);
Texture2D<float3> IndirectRadiance : register(t6);
Texture2D<float4> ProbesIndirectIrradiance : register(t7);
Texture2D<float3> ScreenIndirectIrradiance : register(t8);
RWTexture2D<float3> SceneColor : register(u0);
SamplerComparisonState ShadowMapSampler : register(s0);
SamplerState LinearClamp : register(s1);

float3 GetIndirectIrradiance(RasterSurface surface, uint2 pixel)
{
    if (IndirectIrradianceSource == kScreenIndirectIrradiance)
        return ScreenIndirectIrradiance[pixel];
    if (IndirectIrradianceSource == kProbesIndirectIrradiance)
        return SampleIrradianceProbes(ProbesIndirectIrradiance, LinearClamp, Probes, surface.position, surface.shadingNormal);
    return 0.0;
}

float3 GetIndirectRadiance(RasterSurface surface, float3 view, uint2 pixel)
{
    if (IndirectIrradianceSource == kIndirectIrradianceNone)
        return IndirectRadiance[pixel];

    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, view);
    return kPi * EvaluateGltfDiffuseLobeForIrradiance(brdf) * GetIndirectIrradiance(surface, pixel);
}

float3 GetSceneColor(RasterSurface surface, uint2 pixel)
{
    if (ShadingOutput == kShadingOutputIndirectIrradiance)
        return GetIndirectIrradiance(surface, pixel);

    const float3 view = normalize(ViewerPos - surface.position);
    const float3 indirectRadiance = GetIndirectRadiance(surface, view, pixel);
    if (ShadingOutput == kShadingOutputIndirectRadiance)
        return indirectRadiance;

    const float3 shadowCoord = mul(ShadowMatrix, float4(surface.position, 1.0)).xyz;
    return GetOutgoingRadiance(surface, view, shadowCoord, ShadowMap, ShadowMapSampler, LightDirection, LightIlluminance, indirectRadiance);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height)
        return;

    RasterSurface surface;
    if (!LoadGBuffer(DTid.xy, surface))
        return;

    SceneColor[DTid.xy] = GetSceneColor(surface, DTid.xy);
}
