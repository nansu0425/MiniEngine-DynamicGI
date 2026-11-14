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

inline DirectX::XMUINT3 GetProbeCoordinate(const IrradianceProbeGrid& grid, uint32_t probe)
{
    return DirectX::XMUINT3(probe % grid.counts[0], (probe / grid.counts[0]) % grid.counts[1], probe / (grid.counts[0] * grid.counts[1]));
}

inline uint32_t GetProbeIndex(const IrradianceProbeGrid& grid, const DirectX::XMUINT3& coordinate)
{
    return coordinate.x + (coordinate.y + coordinate.z * grid.counts[1]) * grid.counts[0];
}

inline Math::Vector3 GetProbePosition(const IrradianceProbeGrid& grid, uint32_t probe)
{
    const DirectX::XMUINT3 coordinate = GetProbeCoordinate(grid, probe);
    return grid.origin + Math::Vector3((float)coordinate.x, (float)coordinate.y, (float)coordinate.z) * grid.spacing;
}
