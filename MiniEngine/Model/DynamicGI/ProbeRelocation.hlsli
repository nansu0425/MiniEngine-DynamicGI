#ifndef __PROBE_RELOCATION_HLSLI__
#define __PROBE_RELOCATION_HLSLI__

#include "Model/DynamicGI/ProbePositions.hlsli"

static const uint kProbeRelocationNotMoved = 0;
static const uint kProbeRelocationBackFacePush = 1;
static const uint kProbeRelocationBlocked = 2;

uint GetProbeRelocationResult(ProbePositions positions, uint3 coordinate)
{
    return (uint)positions.offsets[uint2(GetProbeIndex(positions.grid, coordinate), 0)].w;
}

#endif
