#ifndef __PROBE_FACING_HLSLI__
#define __PROBE_FACING_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

float GetProbeFacingWeight(float minFacingWeight, IrradianceProbeGrid grid, uint3 coordinate, float3 position, float3 geometryNormal)
{
    const float3 toProbe = normalize(GetProbePosition(grid, coordinate) - position);
    const float wrappedCosine = (dot(toProbe, geometryNormal) + 1.0) * 0.5;
    return wrappedCosine * wrappedCosine + minFacingWeight;
}

#endif
