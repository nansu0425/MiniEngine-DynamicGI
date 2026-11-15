#include "Model/DynamicGI/ProbeDistanceLine.hlsli"

Texture2D<float2> ProbesDistance : register(t0);
Texture2D<float4> RaySamples : register(t1);
Texture2D<float> ProbesTexelDistance : register(t2);

static const float3 kTexelMeanColor = float3(1.0, 0.85, 0.0);
static const float3 kTexelDistanceColor = float3(1.0, 0.0, 1.0);
static const float3 kRaySampleColor = float3(0.0, 0.8, 1.0);
static const float3 kRaySampleAtMaxDistanceColor = float3(1.0, 0.0, 0.0);

ProbeDistanceLineVertex main(uint vertexID : SV_VertexID, uint lineIndex : SV_InstanceID)
{
    const uint3 coordinate = GetProbeCoordinate(Grid, Probe);
    float3 direction;
    float distance;
    ProbeDistanceLineVertex vertex;
    if (Lines != kProbeDistanceLinesOfRaySamples)
    {
        const uint2 probeTexel = uint2(lineIndex % DistanceTexelsPerSide, lineIndex / DistanceTexelsPerSide);
        const uint2 atlasTexel = GetProbeAtlasTexel(Grid, DistanceTexelsPerSide, coordinate, probeTexel);
        direction = GetProbeTexelDirection(DistanceTexelsPerSide, probeTexel);
        const bool isMean = Lines == kProbeDistanceLinesOfTexelMeans;
        distance = isMean ? ProbesDistance[atlasTexel].x : ProbesTexelDistance[atlasTexel];
        vertex.color = isMean ? kTexelMeanColor : kTexelDistanceColor;
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
