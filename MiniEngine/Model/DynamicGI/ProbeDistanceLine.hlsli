#ifndef __PROBE_DISTANCE_LINE_HLSLI__
#define __PROBE_DISTANCE_LINE_HLSLI__

#include "Model/DynamicGI/ProbeRays.hlsli"

static const uint kProbeDistanceLinesOfTexelMeans = 0;
static const uint kProbeDistanceLinesOfRaySamples = 1;

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    IrradianceProbeGrid Grid;
    float4x4 RayRotation;
    uint RaysPerProbe;
    uint Probe;
    uint Lines;
};

struct ProbeDistanceLineVertex
{
    float4 position : SV_Position;
    nointerpolation float3 color : Color;
};

#endif
