#include "Model/DynamicGI/ProbeRayHit.hlsli"

cbuffer Constants : register(b0)
{
    uint DistanceTexelsPerSide;
    IrradianceProbeGrid Grid;
};

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
    TraceProbeRayHit(Grid, GetProbePosition(Grid, coordinate), GetProbeTexelDirection(DistanceTexelsPerSide, probeTexel), rayHit, distance);
    ProbesTexelDistance[GetProbeAtlasTexel(Grid, DistanceTexelsPerSide, coordinate, probeTexel)] = distance;
}
