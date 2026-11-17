#ifndef __PROBE_OFFSET_LINE_HLSLI__
#define __PROBE_OFFSET_LINE_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    IrradianceProbeGrid Grid;
};

struct ProbeOffsetLineVertex
{
    float4 position : SV_Position;
    nointerpolation float3 color : Color;
};

#endif
