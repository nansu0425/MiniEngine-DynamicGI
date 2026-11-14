#pragma once

#include "Core/pch.h"

__declspec(align(16)) struct IrradianceProbeGrid
{
    Math::Vector3 origin;
    Math::Vector3 spacing;
    uint32_t counts[3];
};

inline uint32_t GetProbeCount(const IrradianceProbeGrid& grid)
{
    return grid.counts[0] * grid.counts[1] * grid.counts[2];
}
