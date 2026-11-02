#ifndef __HIT_SURFACE_HLSLI__
#define __HIT_SURFACE_HLSLI__

#include "Core/Shaders/NormalTexture.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"

// ray가 맞은 곳의 surface. raster의 DefaultVS, DefaultPS가 pixel에서 얻는 값을 같은 data로 복원한다.
// occlusion texture는 ray tracing이 직접 구할 visibility와 겹치므로 읽지 않는다.
struct HitSurface
{
    float3 position;
    // triangle 면의 normal. ray가 온 쪽을 향한다.
    float3 geometryNormal;
    // 보간한 vertex normal. ray가 온 쪽을 향한다.
    float3 vertexNormal;
    // vertexNormal에 normal map을 적용한 normal. ray가 온 쪽을 향한다.
    float3 shadingNormal;
    float2 uv0;
    float4 baseColor;
    float metallic;
    float roughness;
    float3 emissive;
};

float3 InterpolateNormal(ByteAddressBuffer data, uint3 vertexAddresses, uint offset, float3 weights)
{
    return data.Load<float3>(vertexAddresses.x + offset) * weights.x
        + data.Load<float3>(vertexAddresses.y + offset) * weights.y
        + data.Load<float3>(vertexAddresses.z + offset) * weights.z;
}

// w는 bitangent의 부호 (glTF의 TANGENT)이고 xyz처럼 보간한다.
float4 InterpolateTangent(ByteAddressBuffer data, uint3 vertexAddresses, uint offset, float3 weights)
{
    return data.Load<float4>(vertexAddresses.x + offset) * weights.x
        + data.Load<float4>(vertexAddresses.y + offset) * weights.y
        + data.Load<float4>(vertexAddresses.z + offset) * weights.z;
}

// normal은 objectToWorld의 inverse transpose로 옮긴다.
float3 ObjectToWorldNormal(RayHit hit, float3 normal)
{
    return normalize(mul(normal, (float3x3)hit.worldToObject));
}

// textureLod는 material texture를 읽는 mip이다. 따로 정한 query 밖에서는 kSharpestTextureLod다.
HitSurface GetHitSurface(RayHit hit, float textureLod)
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

    surface.baseColor = material.baseColorFactor * SampleMaterialTexture(hitTriangle, kBaseColorTexture, textureLod);
    const float2 metallicRoughness = float2(material.metallicFactor, material.roughnessFactor) * SampleMaterialTexture(hitTriangle, kMetallicRoughnessTexture, textureLod).bg;
    surface.metallic = metallicRoughness.x;
    surface.roughness = metallicRoughness.y;
    surface.emissive = material.emissiveFactor * SampleMaterialTexture(hitTriangle, kEmissiveTexture, textureLod).rgb;

    const float3 vertexNormal = ObjectToWorldNormal(hit, InterpolateNormal(data, vertexAddresses, geometry.normalOffset, weights));
    float3 normal = vertexNormal;
    // normal texture가 없는 material은 surface normal을 그대로 쓴다 (ConstantBuffers.h의 normalTexture).
    if (geometry.tangentOffset != 0 && (material.flags & kNormalTextureFlag) != 0)
    {
        const float4 tangent = InterpolateTangent(data, vertexAddresses, geometry.tangentOffset, weights);
        // tangent는 면 위의 방향이므로 normal과 달리 objectToWorld로 옮긴다.
        const float4 worldTangent = float4(mul((float3x3)hit.objectToWorld, tangent.xyz), tangent.w);
        const float3 normalTextureValue = SampleMaterialTexture(hitTriangle, kNormalTexture, textureLod).xyz;
        normal = normalize(GetNormalTextureNormal(normal, worldTangent, normalTextureValue, material.normalTextureScale));
    }

    // cross(p1 - p0, p2 - p0)는 DXR이 front face로 판정하는 쪽을 향한다.
    const float side = hit.frontFace ? 1.0 : -1.0;
    surface.geometryNormal = side * ObjectToWorldNormal(hit, cross(p1 - p0, p2 - p0));
    surface.vertexNormal = side * vertexNormal;
    surface.shadingNormal = side * normal;
    return surface;
}

#endif
