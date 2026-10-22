#pragma once

#include "Core/pch.h"

namespace Math
{
    class Camera;
}

// camera에서 pixel 격자 위의 각 위치로 가는 ray. CameraRays.hlsli의 CameraRays와 같은 layout이다.
// pixel 격자는 TAA를 끈 raster의 pixel (x, y)가 [x, x + 1) x [y, y + 1)을 덮는 좌표로, TAA jitter와 무관하다.
struct CameraRays
{
    Math::Vector3 origin;
    // pixel 격자의 왼쪽 위 corner로 가는 ray 방향. 다른 위치는 그 corner에서 pixel 단위로 잰 만큼 pixelStepRight, pixelStepDown을 더한다.
    Math::Vector3 topLeftCornerDirection;
    Math::Vector3 pixelStepRight;
    Math::Vector3 pixelStepDown;
    // raster sample 위치를 pixel 격자에서 그 pixel의 왼쪽 위 corner부터 잰 것. raster는 늘 render target pixel의 중심을 sample하지만
    // render target이 pixel 격자에서 TAA jitter만큼 옮겨져 있으므로, 이 값은 jitter를 따라 (0, 1] 안에서 바뀌고 TAA를 끄면 두 격자가 겹쳐 (0.5, 0.5)다.
    DirectX::XMFLOAT2 rasterSampleOffset;
};
static_assert(sizeof(CameraRays) == 80, "CameraRays.hlsli의 CameraRays가 cbuffer에서 차지하는 크기와 다르다");

// rasterViewport는 raster가 scene을 그린 viewport다.
CameraRays DescribeCameraRays(const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport);
