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

// BLAS geometry 하나, 즉 Mesh::Draw 하나의 triangle과 material을 읽을 위치. BottomLevelAS.cpp의 BLASGeometry와 같은 layout이다.
struct BLASGeometry
{
    // data buffer에서 draw의 첫 index의 byte 위치
    uint indexOffset;
    // index 하나의 byte 수. 2 또는 4다.
    uint indexSize;
    // data buffer에서 draw의 첫 vertex의 byte 위치
    uint vertexOffset;
    uint vertexStride;
    // vertex 안에서 attribute의 byte 위치. position이 0에 있으므로 0이면 vertex에 그 attribute가 없다.
    uint normalOffset;
    uint tangentOffset;
    uint uv0Offset;
    uint uv1Offset;
    // material constants 안에서 material의 순서
    uint material;
    // SceneTextures와 SceneSamplers에서 material의 texture 5개가 시작하는 위치
    uint textures;
    uint samplers;
};
static const uint kBLASGeometryStride = 44;

// ConstantBuffers.h의 texture 순서와 같다.
static const uint kBaseColorTexture = 0;
static const uint kMetallicRoughnessTexture = 1;
static const uint kEmissiveTexture = 3;
static const uint kNormalTexture = 4;

// ConstantBuffers.h의 MaterialConstants와 같은 layout이다.
struct MaterialConstants
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float normalTextureScale;
    float metallicFactor;
    float roughnessFactor;
    // texture 순서번째 bit가 그 texture에 UV1을 쓸지 고른다.
    uint flags;
};
static const uint kMaterialConstantsStride = 256;

// 2 byte index는 4 byte 단위로만 읽을 수 있으므로 index 3개를 감싸는 두 word를 읽어 나눈다.
uint3 LoadTriangleIndices(ByteAddressBuffer data, BLASGeometry geometry, uint primitiveIndex)
{
    const uint address = geometry.indexOffset + primitiveIndex * 3 * geometry.indexSize;
    if (geometry.indexSize == 4)
        return data.Load3(address);

    const uint alignedAddress = address & ~3;
    const uint2 words = data.Load2(alignedAddress);
    if (address == alignedAddress)
        return uint3(words.x & 0xFFFF, words.x >> 16, words.y & 0xFFFF);
    return uint3(words.x >> 16, words.y & 0xFFFF, words.y >> 16);
}

// R10G10B10A2_UNORM에 [0, 1]로 담은 [-1, 1] 값을 되돌린다.
float4 UnpackSignedR10G10B10A2(uint packed)
{
    const float4 stored = float4(packed & 0x3FF, (packed >> 10) & 0x3FF, (packed >> 20) & 0x3FF, packed >> 30) / float4(1023.0, 1023.0, 1023.0, 3.0);
    return stored * 2.0 - 1.0;
}

float2 UnpackR16G16Float(uint packed)
{
    return f16tof32(uint2(packed & 0xFFFF, packed >> 16));
}

uint3 LoadPackedAttribute(ByteAddressBuffer data, uint3 vertexAddresses, uint offset)
{
    return uint3(data.Load(vertexAddresses.x + offset), data.Load(vertexAddresses.y + offset), data.Load(vertexAddresses.z + offset));
}

float2 InterpolateUV(ByteAddressBuffer data, uint3 vertexAddresses, uint offset, float3 weights)
{
    const uint3 packed = LoadPackedAttribute(data, vertexAddresses, offset);
    return UnpackR16G16Float(packed.x) * weights.x + UnpackR16G16Float(packed.y) * weights.y + UnpackR16G16Float(packed.z) * weights.z;
}

float4 InterpolateSignedR10G10B10A2(ByteAddressBuffer data, uint3 vertexAddresses, uint offset, float3 weights)
{
    const uint3 packed = LoadPackedAttribute(data, vertexAddresses, offset);
    return UnpackSignedR10G10B10A2(packed.x) * weights.x + UnpackSignedR10G10B10A2(packed.y) * weights.y + UnpackSignedR10G10B10A2(packed.z) * weights.z;
}

// ray에는 화면 미분이 없으므로 mip 0을 읽는다.
float4 SampleMaterialTexture(BLASGeometry geometry, MaterialConstants material, uint texture, float2 uv0, float2 uv1)
{
    const float2 uv = ((material.flags >> texture) & 1) != 0 ? uv1 : uv0;
    return SceneTextures[NonUniformResourceIndex(geometry.textures + texture)]
        .SampleLevel(SceneSamplers[NonUniformResourceIndex(geometry.samplers + texture)], uv, 0.0);
}

// normal은 objectToWorld의 inverse transpose로 옮긴다.
float3 ObjectToWorldNormal(RayHit hit, float3 normal)
{
    return normalize(mul(normal, (float3x3)hit.worldToObject));
}

HitSurface GetHitSurface(RayHit hit)
{
    const BLASInstance instance = BLASInstances[hit.instanceIndex];
    ByteAddressBuffer data = SceneBuffers[NonUniformResourceIndex(instance.modelSRVs + kModelDataSRV)];
    ByteAddressBuffer materials = SceneBuffers[NonUniformResourceIndex(instance.modelSRVs + kModelMaterialsSRV)];
    ByteAddressBuffer geometries = SceneBuffers[NonUniformResourceIndex(instance.modelSRVs + kModelBLASGeometriesSRV)];
    const BLASGeometry geometry = geometries.Load<BLASGeometry>((instance.firstGeometry + hit.geometryIndex) * kBLASGeometryStride);
    const MaterialConstants material = materials.Load<MaterialConstants>(geometry.material * kMaterialConstantsStride);

    const uint3 vertexAddresses = geometry.vertexOffset + LoadTriangleIndices(data, geometry, hit.primitiveIndex) * geometry.vertexStride;
    const float3 weights = float3(1.0 - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics.x, hit.barycentrics.y);

    const float3 p0 = asfloat(data.Load3(vertexAddresses.x));
    const float3 p1 = asfloat(data.Load3(vertexAddresses.y));
    const float3 p2 = asfloat(data.Load3(vertexAddresses.z));

    HitSurface surface;
    surface.position = mul(hit.objectToWorld, float4(p0 * weights.x + p1 * weights.y + p2 * weights.z, 1.0));

    // raster처럼 UV1이 없는 vertex는 모든 texture에 UV0을 쓴다.
    surface.uv0 = geometry.uv0Offset != 0 ? InterpolateUV(data, vertexAddresses, geometry.uv0Offset, weights) : float2(0.0, 0.0);
    const float2 uv1 = geometry.uv1Offset != 0 ? InterpolateUV(data, vertexAddresses, geometry.uv1Offset, weights) : surface.uv0;

    surface.baseColor = material.baseColorFactor * SampleMaterialTexture(geometry, material, kBaseColorTexture, surface.uv0, uv1);
    const float2 metallicRoughness = float2(material.metallicFactor, material.roughnessFactor) * SampleMaterialTexture(geometry, material, kMetallicRoughnessTexture, surface.uv0, uv1).bg;
    surface.metallic = metallicRoughness.x;
    surface.roughness = metallicRoughness.y;
    surface.emissive = material.emissiveFactor * SampleMaterialTexture(geometry, material, kEmissiveTexture, surface.uv0, uv1).rgb;

    float3 normal = ObjectToWorldNormal(hit, InterpolateSignedR10G10B10A2(data, vertexAddresses, geometry.normalOffset, weights).xyz);
    if (geometry.tangentOffset != 0)
    {
        const float4 tangent = InterpolateSignedR10G10B10A2(data, vertexAddresses, geometry.tangentOffset, weights);
        // tangent는 면 위의 방향이므로 normal과 달리 objectToWorld로 옮긴다.
        const float3 worldTangent = normalize(mul((float3x3)hit.objectToWorld, tangent.xyz));
        const float3 worldBitangent = normalize(cross(normal, worldTangent)) * tangent.w;

        float3 textureNormal = SampleMaterialTexture(geometry, material, kNormalTexture, surface.uv0, uv1).xyz * 2.0 - 1.0;
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
