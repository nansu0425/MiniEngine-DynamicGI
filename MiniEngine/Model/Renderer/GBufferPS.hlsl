#include "Model/Renderer/Common.hlsli"
#include "Model/Renderer/RasterMaterial.hlsli"
#include "Model/Renderer/GBuffer.hlsli"

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProj;
    float4x4 ShadowMatrix;
    float3 ViewerPos;
    float3 LightDirection;
    float3 LightIlluminance;
}

[RootSignature(Renderer_RootSig)]
GBufferTargets main(VSOutput vsOutput, bool isFrontFace : SV_IsFrontFace)
{
    const float3 view = normalize(ViewerPos - vsOutput.worldPos);
    const RasterSurface surface = GetRasterSurface(vsOutput, isFrontFace, view);
    if (IsCutOut(surface))
        discard;
    return EncodeGBuffer(surface);
}
