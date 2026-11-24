#pragma once

#include <DirectXMath.h>
#include <cmath>
#include <cstdint>

constexpr uint32_t kProbeGuideTexelsPerSide = 16;
constexpr uint32_t kProbeGuideTexelCount = kProbeGuideTexelsPerSide * kProbeGuideTexelsPerSide;

inline DirectX::XMUINT2 GetProbeGuideTexelCoordinate(uint32_t texel)
{
    return DirectX::XMUINT2(texel % kProbeGuideTexelsPerSide, texel / kProbeGuideTexelsPerSide);
}

inline uint32_t GetProbeGuideTexelIndexAt(const DirectX::XMUINT2& coordinate)
{
    return coordinate.x + coordinate.y * kProbeGuideTexelsPerSide;
}

inline DirectX::XMFLOAT3 GetProbeGuideTexelCenterDirection(uint32_t texel)
{
    const DirectX::XMUINT2 coordinate = GetProbeGuideTexelCoordinate(texel);
    const float foldedX = (coordinate.x + 0.5f) / kProbeGuideTexelsPerSide * 2.0f - 1.0f;
    const float foldedY = (coordinate.y + 0.5f) / kProbeGuideTexelsPerSide * 2.0f - 1.0f;
    DirectX::XMFLOAT3 direction(foldedX, foldedY, 1.0f - std::abs(foldedX) - std::abs(foldedY));
    if (direction.z < 0.0f)
    {
        direction.x = (1.0f - std::abs(foldedY)) * (foldedX >= 0.0f ? 1.0f : -1.0f);
        direction.y = (1.0f - std::abs(foldedX)) * (foldedY >= 0.0f ? 1.0f : -1.0f);
    }
    DirectX::XMStoreFloat3(&direction, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&direction)));
    return direction;
}
