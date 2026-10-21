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

bool TraceClosestHit(RayDesc ray, out RayHit hit)
{
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {}

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

#endif
