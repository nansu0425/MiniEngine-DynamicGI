#pragma once

#include "Core/pch.h"

namespace Math
{
    class Camera;
}

// camera에서 화면의 각 pixel로 가는 ray. CameraRays.hlsli의 CameraRays와 같은 layout이다.
struct CameraRays
{
    Math::Vector3 origin;
    // 왼쪽 위 pixel의 ray 방향. 다른 pixel은 pixel 좌표만큼 pixelStepRight, pixelStepDown을 더한다.
    Math::Vector3 topLeftPixelDirection;
    Math::Vector3 pixelStepRight;
    Math::Vector3 pixelStepDown;
};

// 각 pixel의 ray가, raster가 그 pixel의 coverage와 depth를 정한 sample 위치를 지나게 한다. rasterViewport는 raster가 scene을 그린 viewport다.
CameraRays DescribeCameraRays(const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport);
