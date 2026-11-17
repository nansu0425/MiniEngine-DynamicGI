#include "Model/DynamicGI/ProbeRayHit.hlsli"
#include "Model/DynamicGI/ProbePositions.hlsli"

cbuffer Constants : register(b0)
{
    uint DistanceTexelsPerSide;
    IrradianceProbeGrid Grid;
};

Texture2D<float4> ProbesOffset : register(t0);
RWTexture2D<float> ProbesTexelDistance : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 probeTexel = DTid.xy;
    if (any(probeTexel >= DistanceTexelsPerSide))
        return;

    const uint3 coordinate = GetProbeCoordinate(Grid, DTid.z);
    RayHit rayHit;
    float distance;
    TraceProbeRayHit(Grid, GetProbePosition(MakeProbePositions(Grid, ProbesOffset), coordinate), GetProbeTexelDirection(DistanceTexelsPerSide, probeTexel), rayHit, distance);
    ProbesTexelDistance[GetProbeAtlasTexel(Grid, DistanceTexelsPerSide, coordinate, probeTexel)] = distance;
}
