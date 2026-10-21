#ifndef __RAYTRACING_SCENE_HLSLI__
#define __RAYTRACING_SCENE_HLSLI__

// RaytracingScene의 TLAS에서 ray가 맞은 triangle
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
};

bool TraceClosestHit(RaytracingAccelerationStructure scene, RayDesc ray, out RayHit hit)
{
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
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
    return true;
}

#endif
