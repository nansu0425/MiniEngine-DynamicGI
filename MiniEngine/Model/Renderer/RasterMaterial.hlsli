#ifndef __RASTER_MATERIAL_HLSLI__
#define __RASTER_MATERIAL_HLSLI__

#include "Core/Shaders/NormalTexture.hlsli"
#include "Model/Renderer/RasterSurface.hlsli"

Texture2D<float4> baseColorTexture          : register(t0);
Texture2D<float3> metallicRoughnessTexture  : register(t1);
Texture2D<float3> emissiveTexture           : register(t3);
Texture2D<float3> normalTexture             : register(t4);

SamplerState baseColorSampler               : register(s0);
SamplerState metallicRoughnessSampler       : register(s1);
SamplerState emissiveSampler                : register(s3);
SamplerState normalSampler                  : register(s4);

cbuffer MaterialConstants : register(b0)
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float normalTextureScale;
    float2 metallicRoughnessFactor;
    uint flags;
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
// flags 의 이 bit 가 normal texture 가 있는지다 (ConstantBuffers.h 의 normalTexture).
static const uint NORMAL_TEXTURE_FLAG = 1u << 8;
static const uint ALPHA_TEST_FLAG = 1u << 6;
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
    // normal texture가 없는 material은 surface normal을 그대로 쓴다 (ConstantBuffers.h의 normalTexture).
    if ((flags & NORMAL_TEXTURE_FLAG) == 0)
        return normal;

    // Read normal map and convert to SNORM (TODO:  convert all normal maps to R8G8B8A8_SNORM?)
    return GetNormalTextureNormal(normal, vsOutput.tangent, normalTexture.Sample(normalSampler, UVSET(NORMAL)), normalTextureScale);
#endif
}

// pixel이 놓인 triangle 면의 normal을 view 쪽으로 구한다. vertex normal은 면을 따라 휘므로 면의 방향은 worldPos의 미분으로 구한다.
float3 GetGeometryNormal(float3 worldPos, float3 view)
{
    float3 geometryNormal = cross(ddx(worldPos), ddy(worldPos));
    if (dot(geometryNormal, view) < 0.0)
        geometryNormal = -geometryNormal;
    return normalize(geometryNormal);
}

RasterSurface GetRasterSurface(VSOutput vsOutput, bool isFrontFace, float3 view)
{
    RasterSurface surface;
    surface.position = vsOutput.worldPos;
    surface.geometryNormal = GetGeometryNormal(vsOutput.worldPos, view);

    // GltfBrdf.hlsli는 단위 normal을 받는다. tangent frame을 보간한 normal map의 normal은 길이가 1이 아닐 수 있다.
    surface.shadingNormal = normalize(ComputeNormal(vsOutput));
    // double-sided material의 뒷면에서는 뒤집은 normal로 반사한다.
    if (!isFrontFace)
        surface.shadingNormal = -surface.shadingNormal;

    // Load and modulate textures
    surface.baseColor = baseColorFactor * baseColorTexture.Sample(baseColorSampler, UVSET(BASECOLOR));
    const float2 metallicRoughness = metallicRoughnessFactor * metallicRoughnessTexture.Sample(metallicRoughnessSampler, UVSET(METALLICROUGHNESS)).bg;
    surface.metallic = metallicRoughness.x;
    surface.roughness = metallicRoughness.y;
    surface.emissive = emissiveFactor * emissiveTexture.Sample(emissiveSampler, UVSET(EMISSIVE));
    return surface;
}

bool IsCutOut(RasterSurface surface)
{
    const float cutoff = f16tof32(flags >> 16);
    return (flags & ALPHA_TEST_FLAG) != 0 && surface.baseColor.a < cutoff;
}

#endif
