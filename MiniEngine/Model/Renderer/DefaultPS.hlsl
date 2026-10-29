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
#include "Core/Shaders/SurfaceShading.hlsli"

Texture2D<float4> baseColorTexture          : register(t0);
Texture2D<float3> metallicRoughnessTexture  : register(t1);
Texture2D<float3> emissiveTexture           : register(t3);
Texture2D<float3> normalTexture             : register(t4);

SamplerState baseColorSampler               : register(s0);
SamplerState metallicRoughnessSampler       : register(s1);
SamplerState emissiveSampler                : register(s3);
SamplerState normalSampler                  : register(s4);

Texture2D<float> texShadow			    : register(t13);

cbuffer MaterialConstants : register(b0)
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float normalTextureScale;
    float2 metallicRoughnessFactor;
    uint flags;
}

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProj;
    float4x4 ShadowMatrix;
    float3 ViewerPos;
    float3 LightDirection;
    float3 LightIlluminance;
}

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
#ifndef NO_TANGENT_FRAME
    float4 tangent : TANGENT;
#endif
    float2 uv0 : TEXCOORD0;
#ifndef NO_SECOND_UV
    float2 uv1 : TEXCOORD1;
#endif
    float3 worldPos : TEXCOORD2;
    float3 shadowCoord : TEXCOORD3;
};

// Flag helpers
static const uint BASECOLOR = 0;
static const uint METALLICROUGHNESS = 1;
static const uint OCCLUSION = 2;
static const uint EMISSIVE = 3;
static const uint NORMAL = 4;
#ifdef NO_SECOND_UV
#define UVSET( offset ) vsOutput.uv0
#else
#define UVSET( offset ) lerp(vsOutput.uv0, vsOutput.uv1, (flags >> offset) & 1)
#endif

float3 ComputeNormal(VSOutput vsOutput)
{
    float3 normal = normalize(vsOutput.normal);

#ifdef NO_TANGENT_FRAME
    return normal;
#else
    // Construct tangent frame
    float3 tangent = normalize(vsOutput.tangent.xyz);
    float3 bitangent = normalize(cross(normal, tangent)) * vsOutput.tangent.w;
    float3x3 tangentFrame = float3x3(tangent, bitangent, normal);

    // Read normal map and convert to SNORM (TODO:  convert all normal maps to R8G8B8A8_SNORM?)
    normal = normalTexture.Sample(normalSampler, UVSET(NORMAL)) * 2.0 - 1.0;

    // glTF spec says to normalize N before and after scaling, but that's excessive
    normal = normalize(normal * float3(normalTextureScale, normalTextureScale, 1));

    // Multiply by transpose (reverse order)
    return mul(normal, tangentFrame);
#endif
}

// pixel이 놓인 triangle 면의 normal을 view 쪽으로 구한다. vertex normal은 면을 따라 휘므로 면의 방향은 worldPos의 미분으로 구한다.
float3 GetGeometryNormal(float3 worldPos, float3 view)
{
    float3 geometryNormal = cross(ddx(worldPos), ddy(worldPos));
    if (dot(geometryNormal, view) < 0.0)
        geometryNormal = -geometryNormal;
    return geometryNormal;
}

[RootSignature(Renderer_RootSig)]
float4 main(VSOutput vsOutput, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
    // Load and modulate textures
    float4 baseColor = baseColorFactor * baseColorTexture.Sample(baseColorSampler, UVSET(BASECOLOR));
    float2 metallicRoughness = metallicRoughnessFactor * 
        metallicRoughnessTexture.Sample(metallicRoughnessSampler, UVSET(METALLICROUGHNESS)).bg;

    SurfaceShadingInput surface;
    surface.baseColor = baseColor.rgb;
    surface.metallic = metallicRoughness.x;
    surface.roughness = metallicRoughness.y;
    surface.emissive = emissiveFactor * emissiveTexture.Sample(emissiveSampler, UVSET(EMISSIVE));
    // GltfBrdf.hlsli는 단위 normal을 받는다. tangent frame을 보간한 normal map의 normal은 길이가 1이 아닐 수 있다.
    surface.normal = normalize(ComputeNormal(vsOutput));
    if (!isFrontFace)
        surface.normal = -surface.normal;
    const float3 view = normalize(ViewerPos - vsOutput.worldPos);

    const float lightVisibility = GetDirectionalLightVisibility(texShadow, shadowSampler, vsOutput.shadowCoord,
        GetGeometryNormal(vsOutput.worldPos, view), LightDirection);
    const float3 radiance = GetOutgoingRadiance(surface, view, LightDirection, LightIlluminance, lightVisibility);

    // TODO: Shade each light using Forward+ tiles

    return float4(radiance, baseColor.a);
}
