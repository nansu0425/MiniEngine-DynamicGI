#ifndef __PROBE_DISTANCE_LINE_HLSLI__
#define __PROBE_DISTANCE_LINE_HLSLI__

#include "Model/DynamicGI/ProbeRays.hlsli"

static const uint kProbeDistanceLinesOfTexelMeans = 0;
static const uint kProbeDistanceLinesOfRaySamples = 1;
static const uint kProbeDistanceLinesOfTexelDistances = 2;

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    uint Probe;
    uint Lines;
    uint DistanceTexelsPerSide;
    uint HighlightedRay;
    IrradianceProbeGrid Grid;
};

static const uint kNoHighlightedRay = 0xffffffff;

struct ProbeDistanceLineVertex
{
    float4 position : SV_Position;
    nointerpolation float3 color : Color;
    nointerpolation float opacity : Opacity;
};

#endif
