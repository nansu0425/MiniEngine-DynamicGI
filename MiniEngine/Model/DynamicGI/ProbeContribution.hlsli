#ifndef __PROBE_CONTRIBUTION_HLSLI__
#define __PROBE_CONTRIBUTION_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

struct ProbeCornerContribution
{
    uint3 coordinate;
    float trilinearWeight;
    float visibilityWeight;
    float weight;
    float3 irradiance;
};

struct SurfaceProbeContribution
{
    uint hasSurface;
    float3 position;
    float3 geometryNormal;
    float3 shadingNormal;
    uint ignoresVisibility;
    float3 irradiance;
    float3 shadingIrradiance;
    ProbeCornerContribution corners[kNumProbeCorners];
};

#endif
