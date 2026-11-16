#ifndef __PROBE_CLASSIFICATION_HLSLI__
#define __PROBE_CLASSIFICATION_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

float GetProbeBackFaceRatio(Texture2D<float> backFaceRatios, IrradianceProbeGrid grid, uint3 coordinate)
{
    return backFaceRatios[uint2(GetProbeIndex(grid, coordinate), 0)];
}

float GetProbeActiveWeight(uint classification, float maxBackFaceRatio, Texture2D<float> backFaceRatios, IrradianceProbeGrid grid, uint3 coordinate)
{
    if (classification == 0)
        return 1.0;
    return GetProbeBackFaceRatio(backFaceRatios, grid, coordinate) <= maxBackFaceRatio ? 1.0 : 0.0;
}

#endif
