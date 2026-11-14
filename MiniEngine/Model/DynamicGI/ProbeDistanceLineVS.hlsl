#include "Model/DynamicGI/ProbeDistanceLine.hlsli"

Texture2D<float2> ProbesDistance : register(t0);
Texture2D<float4> RaySamples : register(t1);

static const float3 kTexelMeanColor = float3(1.0, 0.85, 0.0);
static const float3 kRaySampleColor = float3(0.0, 0.8, 1.0);
static const float3 kRaySampleAtMaxDistanceColor = float3(1.0, 0.0, 0.0);

ProbeDistanceLineVertex main(uint vertexID : SV_VertexID, uint lineIndex : SV_InstanceID)
{
    const uint3 coordinate = GetProbeCoordinate(Grid, Probe);
    float3 direction;
    float distance;
    ProbeDistanceLineVertex vertex;
    if (Lines == kProbeDistanceLinesOfTexelMeans)
    {
        const uint2 probeTexel = uint2(lineIndex % kProbeTexelsPerSide, lineIndex / kProbeTexelsPerSide);
        direction = GetProbeTexelDirection(probeTexel);
        distance = ProbesDistance[GetProbeAtlasTexel(Grid, coordinate, probeTexel)].x;
        vertex.color = kTexelMeanColor;
    }
    else
    {
        ProbeRays rays;
        rays.grid = Grid;
        rays.rotation = RayRotation;
        rays.raysPerProbe = RaysPerProbe;
        direction = GetProbeRayDirection(rays, lineIndex);
        distance = RaySamples[uint2(lineIndex, Probe)].a;
        vertex.color = distance >= GetProbeRayMaxDistance(Grid) ? kRaySampleAtMaxDistanceColor : kRaySampleColor;
    }

    const float3 position = GetProbePosition(Grid, coordinate) + direction * distance * vertexID;
    vertex.position = mul(ViewProj, float4(position, 1.0));
    return vertex;
}
