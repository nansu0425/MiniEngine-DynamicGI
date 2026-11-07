#pragma once

#include "Core/pch.h"

namespace Math
{
    class Camera;
}

// camera에서 viewport로 쏘는 ray를 계산하기 위한 데이터
struct CameraRays
{
    Math::Vector3 origin;
    Math::Vector3 viewportCornerDirection;
    Math::Vector3 pixelStepRight;
    Math::Vector3 pixelStepDown;
    DirectX::XMFLOAT2 viewportOffset;
};
static_assert(sizeof(CameraRays) == 80, "CameraRays.hlsli의 CameraRays가 cbuffer에서 차지하는 크기와 다르다");

CameraRays DescribeCameraRays(const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport);
