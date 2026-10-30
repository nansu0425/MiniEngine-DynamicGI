#ifndef __RAYTRACING_SCENE_HLSLI__
#define __RAYTRACING_SCENE_HLSLI__

// register와 space는 RaytracingScene::InitRootParameters와 같다.

RaytracingAccelerationStructure Scene : register(t0, space1);

// TLAS 안의 BLAS instance 하나가 가리키는 model data. TLAS 안에서 그 BLAS instance의 순서로 찾는다.
struct BLASInstance
{
    // Model::m_RaytracingSRVs 값
    uint modelSRVs;
    // Model::m_MeshFirstBLASGeometry 중 이 BLAS instance가 된 mesh의 값
    uint firstGeometry;
};

// modelSRVs에 더해 SceneBuffers에서 model의 각 buffer를 고른다. Model::m_RaytracingSRVs의 순서와 같다.
static const uint kModelDataSRV = 0;
static const uint kModelMaterialsSRV = 1;
static const uint kModelBLASGeometriesSRV = 2;

StructuredBuffer<BLASInstance> BLASInstances : register(t1, space1);

// Renderer의 scene descriptor heap 전체를 가리킨다.
ByteAddressBuffer SceneBuffers[] : register(t0, space2);
Texture2D<float4> SceneTextures[] : register(t0, space3);
SamplerState SceneSamplers[] : register(s0, space1);

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
    // texture 순서번째 bit가 그 texture에 UV1을 쓸지 고른다. kNormalTextureFlag는 normal texture가 있는지다 (ConstantBuffers.h의 normalTexture).
    // 위 16 bit는 alpha cutoff를 half로 담는다.
    uint flags;
};
static const uint kNormalTextureFlag = 1u << 8;
static const uint kMaterialConstantsStride = 256;

// ray가 맞은 triangle의 vertex와 material을 읽을 위치와 맞은 곳의 UV
struct HitTriangle
{
    // SceneBuffers에서 triangle이 속한 model의 data buffer 위치
    uint dataSRV;
    BLASGeometry geometry;
    MaterialConstants material;
    // data buffer에서 triangle의 세 vertex의 byte 위치
    uint3 vertexAddresses;
    // 맞은 곳에서 세 vertex의 값을 섞는 비율
    float3 weights;
    float2 uv0;
    float2 uv1;
};

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

uint3 LoadPackedAttribute(ByteAddressBuffer data, uint3 vertexAddresses, uint offset)
{
    return uint3(data.Load(vertexAddresses.x + offset), data.Load(vertexAddresses.y + offset), data.Load(vertexAddresses.z + offset));
}

float2 InterpolateUV(ByteAddressBuffer data, uint3 vertexAddresses, uint offset, float3 weights)
{
    return data.Load<float2>(vertexAddresses.x + offset) * weights.x
        + data.Load<float2>(vertexAddresses.y + offset) * weights.y
        + data.Load<float2>(vertexAddresses.z + offset) * weights.z;
}

// committed hit와 traversal 중의 candidate가 같은 값으로 triangle을 가리키므로 둘 다 이 함수로 읽는다.
HitTriangle GetHitTriangle(uint instanceIndex, uint geometryIndex, uint primitiveIndex, float2 barycentrics)
{
    const BLASInstance instance = BLASInstances[instanceIndex];
    ByteAddressBuffer materials = SceneBuffers[NonUniformResourceIndex(instance.modelSRVs + kModelMaterialsSRV)];
    ByteAddressBuffer geometries = SceneBuffers[NonUniformResourceIndex(instance.modelSRVs + kModelBLASGeometriesSRV)];

    HitTriangle hitTriangle;
    hitTriangle.dataSRV = instance.modelSRVs + kModelDataSRV;
    ByteAddressBuffer data = SceneBuffers[NonUniformResourceIndex(hitTriangle.dataSRV)];
    hitTriangle.geometry = geometries.Load<BLASGeometry>((instance.firstGeometry + geometryIndex) * kBLASGeometryStride);
    hitTriangle.material = materials.Load<MaterialConstants>(hitTriangle.geometry.material * kMaterialConstantsStride);

    const BLASGeometry geometry = hitTriangle.geometry;
    hitTriangle.vertexAddresses = geometry.vertexOffset + LoadTriangleIndices(data, geometry, primitiveIndex) * geometry.vertexStride;
    hitTriangle.weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

    // raster처럼 UV1이 없는 vertex는 모든 texture에 UV0을 쓴다.
    hitTriangle.uv0 = geometry.uv0Offset != 0 ? InterpolateUV(data, hitTriangle.vertexAddresses, geometry.uv0Offset, hitTriangle.weights) : float2(0.0, 0.0);
    hitTriangle.uv1 = geometry.uv1Offset != 0 ? InterpolateUV(data, hitTriangle.vertexAddresses, geometry.uv1Offset, hitTriangle.weights) : hitTriangle.uv0;
    return hitTriangle;
}

// ray에는 화면 미분이 없어 맞은 곳의 footprint를 모르므로, 따로 정한 query 밖에서는 가장 선명한 mip 0을 읽는다.
static const float kSharpestTextureLod = 0.0;

// lod는 읽을 mip이다.
float4 SampleMaterialTexture(HitTriangle hitTriangle, uint texture, float lod)
{
    const float2 uv = ((hitTriangle.material.flags >> texture) & 1) != 0 ? hitTriangle.uv1 : hitTriangle.uv0;
    return SceneTextures[NonUniformResourceIndex(hitTriangle.geometry.textures + texture)]
        .SampleLevel(SceneSamplers[NonUniformResourceIndex(hitTriangle.geometry.samplers + texture)], uv, lod);
}

// glTF처럼 base color의 alpha가 alpha cutoff 이상인 곳만 ray를 막는다.
// OPAQUE flag가 없는 BLAS geometry는 alpha test material뿐이므로 traversal 중의 candidate는 모두 이 판정을 받는다.
// alpha test는 shading을 다른 mip으로 하는 query (SurfaceShading.hlsli의 kProbeHitTextureLod)에서도 mip 0으로 본다. 그래서 probe hit의
// ray도 다른 ray와 같은 면을 맞는다. G3D의 probe ray가 alpha test를 어느 mip으로 하는지는 확인하지 않았고, mip 0은 이 repo의 전제다.
bool IsOpaqueAt(uint instanceIndex, uint geometryIndex, uint primitiveIndex, float2 barycentrics)
{
    const HitTriangle hitTriangle = GetHitTriangle(instanceIndex, geometryIndex, primitiveIndex, barycentrics);
    const float alpha = hitTriangle.material.baseColorFactor.a * SampleMaterialTexture(hitTriangle, kBaseColorTexture, kSharpestTextureLod).a;
    return alpha >= f16tof32(hitTriangle.material.flags >> 16);
}

// 어느 query가 이 flag로 쏘는지는 README.md의 "camera 밖에서 본 doubleSided"가 정한다.
// doubleSided material의 BLAS instance는 RaytracingScene이 culling을 끈다.
static const uint kCullSingleSidedBackFaces = RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

// rayFlags는 kCullSingleSidedBackFaces처럼 query마다 고르는 ray flag다.
// scene에 procedural primitive가 없으므로 RayQueryType은 RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES를 갖는다.
template<typename RayQueryType>
RayQueryType TraceScene(RayDesc ray, uint rayFlags)
{
    RayQueryType query;
    query.TraceRayInline(Scene, rayFlags, 0xFF, ray);
    while (query.Proceed())
    {
        if (IsOpaqueAt(query.CandidateInstanceIndex(), query.CandidateGeometryIndex(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics()))
            query.CommitNonOpaqueTriangleHit();
    }
    return query;
}

// TLAS에서 ray가 맞은 triangle
struct RayHit
{
    float t;
    // RaytracingScene에 BLAS instance를 넣은 순서
    uint instanceIndex;
    // Mesh::draw 순서
    uint geometryIndex;
    // draw 안의 triangle 순서
    uint primitiveIndex;
    float2 barycentrics;
    bool frontFace;
    float3x4 objectToWorld;
    float3x4 worldToObject;
};

bool TraceClosestHit(RayDesc ray, uint rayFlags, out RayHit hit)
{
    typedef RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> ClosestHitQuery;
    ClosestHitQuery query = TraceScene<ClosestHitQuery>(ray, rayFlags);

    hit = (RayHit)0;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return false;

    hit.t = query.CommittedRayT();
    hit.instanceIndex = query.CommittedInstanceIndex();
    hit.geometryIndex = query.CommittedGeometryIndex();
    hit.primitiveIndex = query.CommittedPrimitiveIndex();
    hit.barycentrics = query.CommittedTriangleBarycentrics();
    hit.frontFace = query.CommittedTriangleFrontFace();
    hit.objectToWorld = query.CommittedObjectToWorld3x4();
    hit.worldToObject = query.CommittedWorldToObject3x4();
    return true;
}

bool HitsSameTriangle(RayDesc ray, uint rayFlags, bool rayHit, RayHit closestHit)
{
    RayHit otherHit;
    if (TraceClosestHit(ray, rayFlags, otherHit) != rayHit)
        return false;
    return !rayHit || (otherHit.instanceIndex == closestHit.instanceIndex && otherHit.geometryIndex == closestHit.geometryIndex
        && otherHit.primitiveIndex == closestHit.primitiveIndex);
}

// rayFlags로 쏜 ray가 맞은 것 (rayHit, closestHit) 을 alpha test가 정했는지. raster의 cutout은 mip chain을, ray는 mip 0을 읽으므로
// 이런 ray는 raster와 다른 것을 맞을 수 있다. FORCE_OPAQUE는 ray가 지나간 cutout을, CULL_NON_OPAQUE는 ray가 맞은 cutout을 찾는다.
// 어느 쪽이든 맞은 triangle이 바뀌면 alpha test가 이 ray의 결과를 정한 것이다.
bool IsDecidedByAlphaTest(RayDesc ray, uint rayFlags, bool rayHit, RayHit closestHit)
{
    return !HitsSameTriangle(ray, rayFlags | RAY_FLAG_FORCE_OPAQUE, rayHit, closestHit)
        || !HitsSameTriangle(ray, rayFlags | RAY_FLAG_CULL_NON_OPAQUE, rayHit, closestHit);
}

// ray의 TMin과 TMax 사이에 ray를 막는 것이 있는지 알려 준다. 두 점 사이를 볼 때는 TMax를 두 점의 거리로 둔다.
bool IsOccluded(RayDesc ray, uint rayFlags)
{
    typedef RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> OcclusionQuery;
    return TraceScene<OcclusionQuery>(ray, rayFlags).CommittedStatus() != COMMITTED_NOTHING;
}

#endif
