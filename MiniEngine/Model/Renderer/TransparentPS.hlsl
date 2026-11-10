//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):	James Stanard
//              Justin Saunders (ATG)

#include "Model/Renderer/Common.hlsli"
#include "Model/Renderer/RasterMaterial.hlsli"
#include "Model/Renderer/SurfaceRadiance.hlsli"

Texture2D<float> texShadow			    : register(t13);
Texture2D<float3> indirectRadianceTexture   : register(t18);

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProj;
    float4x4 ShadowMatrix;
    float3 ViewerPos;
    float3 LightDirection;
    float3 LightIlluminance;
}

[RootSignature(Renderer_RootSig)]
float4 main(VSOutput vsOutput, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
    const float3 view = normalize(ViewerPos - vsOutput.worldPos);
    const RasterSurface surface = GetRasterSurface(vsOutput, isFrontFace, view);

    // indirect light
    const float3 indirectRadiance = indirectRadianceTexture[uint2(vsOutput.position.xy)];

    const float3 outgoingRadiance = GetOutgoingRadiance(surface, view, vsOutput.shadowCoord, texShadow, shadowSampler,
        LightDirection, LightIlluminance, indirectRadiance);

    return float4(outgoingRadiance, surface.baseColor.a);
}
