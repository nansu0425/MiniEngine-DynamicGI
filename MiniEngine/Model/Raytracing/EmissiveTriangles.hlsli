#ifndef __EMISSIVE_TRIANGLES_HLSLI__
#define __EMISSIVE_TRIANGLES_HLSLI__

#include "Model/Raytracing/RaytracingScene.hlsli"

struct EmissiveTriangle
{
    float3 p0;
    uint instanceIndex;
    float3 p1;
    uint geometryIndex;
    float3 p2;
    uint primitiveIndex;
    float probability;
    float cumulativeProbability;
};

struct EmissivePoint
{
    float3 position;
    float areaPdf;
    EmissiveTriangle source;
};

uint FindEmissiveTriangle(StructuredBuffer<EmissiveTriangle> triangles, uint count, float u)
{
    uint first = 0;
    uint last = count - 1;
    while (first < last)
    {
        const uint middle = (first + last) / 2;
        if (u < triangles[middle].cumulativeProbability)
            last = middle;
        else
            first = middle + 1;
    }
    return first;
}

EmissivePoint SampleEmissivePoint(StructuredBuffer<EmissiveTriangle> triangles, uint count, float3 u)
{
    EmissivePoint emissivePoint;
    emissivePoint.source = triangles[FindEmissiveTriangle(triangles, count, u.x)];
    const EmissiveTriangle source = emissivePoint.source;
    const float radius = sqrt(u.y);
    emissivePoint.position = source.p0 * (1.0 - radius) + source.p1 * (radius * (1.0 - u.z)) + source.p2 * (radius * u.z);
    const float area = 0.5 * length(cross(source.p1 - source.p0, source.p2 - source.p0));
    emissivePoint.areaPdf = source.probability / area;
    return emissivePoint;
}

bool HitsEmissiveTriangle(RayHit hit, EmissiveTriangle source)
{
    return hit.instanceIndex == source.instanceIndex && hit.geometryIndex == source.geometryIndex && hit.primitiveIndex == source.primitiveIndex;
}

#endif
