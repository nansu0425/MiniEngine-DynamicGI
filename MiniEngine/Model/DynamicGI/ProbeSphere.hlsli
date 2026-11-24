#ifndef __PROBE_SPHERE_HLSLI__
#define __PROBE_SPHERE_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    IrradianceProbeGrid Grid;
    float3 CameraPosition;
    float Radius;
    uint Classification;
    float MaxBackFaceRatio;
    uint ShowGuideTexelBorders;
    uint HighlightedProbe;
    uint HighlightedGuideTexel;
};

struct ProbeQuadVertex
{
    noperspective centroid float4 position : SV_Position;
    float3 worldPosition : WorldPosition;
    nointerpolation float3 probePosition : ProbePosition;
    nointerpolation uint3 probeCoordinate : ProbeCoordinate;
};

#endif
