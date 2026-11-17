#ifndef __PROBE_CLASSIFICATION_HLSLI__
#define __PROBE_CLASSIFICATION_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

float GetProbeBackFaceRatio(Texture2D<float> backFaceRatios, IrradianceProbeGrid grid, uint3 coordinate)
{
    return backFaceRatios[uint2(GetProbeIndex(grid, coordinate), 0)];
}

bool IsProbeInsideGeometry(float maxBackFaceRatio, float backFaceRatio)
{
    return backFaceRatio > maxBackFaceRatio;
}

float GetProbeActiveWeight(uint classification, float maxBackFaceRatio, Texture2D<float> backFaceRatios, IrradianceProbeGrid grid, uint3 coordinate)
{
    if (classification == 0)
        return 1.0;
    return IsProbeInsideGeometry(maxBackFaceRatio, GetProbeBackFaceRatio(backFaceRatios, grid, coordinate)) ? 0.0 : 1.0;
}

#endif
