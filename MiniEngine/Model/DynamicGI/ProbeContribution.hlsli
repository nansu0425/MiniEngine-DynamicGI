#ifndef __PROBE_CONTRIBUTION_HLSLI__
#define __PROBE_CONTRIBUTION_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

struct ProbeCornerContribution
{
    uint3 coordinate;
    float trilinearWeight;
    float backFaceRatio;
    float activeWeight;
    float visibilityWeight;
    float facingWeight;
    float weight;
    float3 irradiance;
    float distance;
    float distanceMean;
};

struct SurfaceProbeContribution
{
    uint hasSurface;
    float3 position;
    float3 distanceLookupPosition;
    float3 geometryNormal;
    float3 shadingNormal;
    uint comparesDistance;
    uint fallsBackToTrilinear;
    float3 irradiance;
    float3 shadingIrradiance;
    ProbeCornerContribution corners[kNumProbeCorners];
};

#endif
