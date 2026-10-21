#ifndef __HIT_SURFACE_HLSLI__
#define __HIT_SURFACE_HLSLI__

#include "Model/Raytracing/RaytracingScene.hlsli"

// ray가 맞은 곳의 surface. raster의 DefaultVS, DefaultPS가 pixel에서 얻는 값을 같은 data로 복원한다.
// occlusion texture는 ray tracing이 직접 구할 visibility와 겹치므로 읽지 않는다.
struct HitSurface
{
    float3 position;
    // triangle 면의 normal. ray가 온 쪽을 향한다.
    float3 geometryNormal;
    // 보간한 vertex normal에 normal map을 적용한 normal. ray가 온 쪽을 향한다.
    float3 shadingNormal;
    float2 uv0;
    float4 baseColor;
    float metallic;
    float roughness;
    float3 emissive;
};

// R10G10B10A2_UNORM에 [0, 1]로 담은 [-1, 1] 값을 되돌린다.
float4 UnpackSignedR10G10B10A2(uint packed)
{
    const float4 stored = float4(packed & 0x3FF, (packed >> 10) & 0x3FF, (packed >> 20) & 0x3FF, packed >> 30) / float4(1023.0, 1023.0, 1023.0, 3.0);
    return stored * 2.0 - 1.0;
}

float4 InterpolateSignedR10G10B10A2(ByteAddressBuffer data, uint3 vertexAddresses, uint offset, float3 weights)
{
    const uint3 packed = LoadPackedAttribute(data, vertexAddresses, offset);
    return UnpackSignedR10G10B10A2(packed.x) * weights.x + UnpackSignedR10G10B10A2(packed.y) * weights.y + UnpackSignedR10G10B10A2(packed.z) * weights.z;
}

// normal은 objectToWorld의 inverse transpose로 옮긴다.
float3 ObjectToWorldNormal(RayHit hit, float3 normal)
{
    return normalize(mul(normal, (float3x3)hit.worldToObject));
}

HitSurface GetHitSurface(RayHit hit)
{
    const HitTriangle hitTriangle = GetHitTriangle(hit.instanceIndex, hit.geometryIndex, hit.primitiveIndex, hit.barycentrics);
    const BLASGeometry geometry = hitTriangle.geometry;
    const MaterialConstants material = hitTriangle.material;
    const uint3 vertexAddresses = hitTriangle.vertexAddresses;
    const float3 weights = hitTriangle.weights;
    ByteAddressBuffer data = SceneBuffers[NonUniformResourceIndex(hitTriangle.dataSRV)];

    const float3 p0 = asfloat(data.Load3(vertexAddresses.x));
    const float3 p1 = asfloat(data.Load3(vertexAddresses.y));
    const float3 p2 = asfloat(data.Load3(vertexAddresses.z));

    HitSurface surface;
    surface.position = mul(hit.objectToWorld, float4(p0 * weights.x + p1 * weights.y + p2 * weights.z, 1.0));
    surface.uv0 = hitTriangle.uv0;

    surface.baseColor = material.baseColorFactor * SampleMaterialTexture(hitTriangle, kBaseColorTexture);
    const float2 metallicRoughness = float2(material.metallicFactor, material.roughnessFactor) * SampleMaterialTexture(hitTriangle, kMetallicRoughnessTexture).bg;
    surface.metallic = metallicRoughness.x;
    surface.roughness = metallicRoughness.y;
    surface.emissive = material.emissiveFactor * SampleMaterialTexture(hitTriangle, kEmissiveTexture).rgb;

    float3 normal = ObjectToWorldNormal(hit, InterpolateSignedR10G10B10A2(data, vertexAddresses, geometry.normalOffset, weights).xyz);
    if (geometry.tangentOffset != 0)
    {
        const float4 tangent = InterpolateSignedR10G10B10A2(data, vertexAddresses, geometry.tangentOffset, weights);
        // tangent는 면 위의 방향이므로 normal과 달리 objectToWorld로 옮긴다.
        const float3 worldTangent = normalize(mul((float3x3)hit.objectToWorld, tangent.xyz));
        const float3 worldBitangent = normalize(cross(normal, worldTangent)) * tangent.w;

        float3 textureNormal = SampleMaterialTexture(hitTriangle, kNormalTexture).xyz * 2.0 - 1.0;
        textureNormal = normalize(textureNormal * float3(material.normalTextureScale, material.normalTextureScale, 1.0));
        normal = normalize(mul(textureNormal, float3x3(worldTangent, worldBitangent, normal)));
    }

    // cross(p1 - p0, p2 - p0)는 DXR이 front face로 판정하는 쪽을 향한다.
    const float side = hit.frontFace ? 1.0 : -1.0;
    surface.geometryNormal = side * ObjectToWorldNormal(hit, cross(p1 - p0, p2 - p0));
    surface.shadingNormal = side * normal;
    return surface;
}

#endif
