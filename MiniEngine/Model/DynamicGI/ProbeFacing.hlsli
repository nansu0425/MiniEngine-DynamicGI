#ifndef __PROBE_FACING_HLSLI__
#define __PROBE_FACING_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

static const uint kProbeFacingNormalGeometry = 0;
static const uint kProbeFacingNormalShading = 1;

float GetProbeFacingWeight(float minFacingWeight, IrradianceProbeGrid grid, uint3 coordinate, float3 position, float3 normal)
{
    const float3 toProbe = normalize(GetProbePosition(grid, coordinate) - position);
    const float wrappedCosine = (dot(toProbe, normal) + 1.0) * 0.5;
    return wrappedCosine * wrappedCosine + minFacingWeight;
}

#endif
