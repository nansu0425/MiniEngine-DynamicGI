#ifndef __PROBE_GUIDE_LINE_HLSLI__
#define __PROBE_GUIDE_LINE_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    uint Probe;
    uint HighlightedTexel;
    float Length;
    IrradianceProbeGrid Grid;
};

struct ProbeGuideLineVertex
{
    float4 position : SV_Position;
    nointerpolation float3 color : Color;
    nointerpolation float opacity : Opacity;
};

#endif
