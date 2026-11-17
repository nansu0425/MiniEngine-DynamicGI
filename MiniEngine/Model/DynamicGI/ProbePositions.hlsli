#ifndef __PROBE_POSITIONS_HLSLI__
#define __PROBE_POSITIONS_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

struct ProbePositions
{
    IrradianceProbeGrid grid;
    Texture2D<float4> offsets;
};

ProbePositions MakeProbePositions(IrradianceProbeGrid grid, Texture2D<float4> offsets)
{
    ProbePositions positions;
    positions.grid = grid;
    positions.offsets = offsets;
    return positions;
}

float3 GetProbeOffset(ProbePositions positions, uint3 coordinate)
{
    return positions.offsets[uint2(GetProbeIndex(positions.grid, coordinate), 0)].xyz;
}

float3 GetProbePosition(ProbePositions positions, uint3 coordinate)
{
    return GetProbeGridPosition(positions.grid, coordinate) + GetProbeOffset(positions, coordinate);
}

#endif
