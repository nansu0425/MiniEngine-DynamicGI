#ifndef __PROBE_CONTRIBUTION_RIBBON_HLSLI__
#define __PROBE_CONTRIBUTION_RIBBON_HLSLI__

#include "Model/DynamicGI/ProbeContribution.hlsli"

static const uint kNoHoveredCorner = 0xffffffff;
static const uint kNumCellEdges = 12;
static const uint kFirstCellEdgeRibbon = kNumProbeCorners;
static const uint kNormalRibbon = kFirstCellEdgeRibbon + kNumCellEdges;
static const uint kNumRibbons = kNormalRibbon + 1;

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    IrradianceProbeGrid Grid;
    float2 ViewportSize;
    float NearClip;
    float NormalLength;
    uint HoveredCorner;
};

struct ProbeContributionRibbonVertex
{
    float4 position : SV_Position;
    nointerpolation float3 color : Color;
    nointerpolation uint dashed : Dashed;
    noperspective float pixelsAlong : PixelsAlong;
};

#endif
