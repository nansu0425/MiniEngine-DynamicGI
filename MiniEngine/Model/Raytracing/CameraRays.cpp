#include "Model/Raytracing/CameraRays.h"
#include "Core/Camera/Camera.h"

using namespace Math;

CameraRays DescribeCameraRays(const Camera& camera, const D3D12_VIEWPORT& rasterViewport)
{
    const float halfScreenHeight = tanf(camera.GetFOV() * 0.5f);
    const float halfScreenWidth = halfScreenHeight * rasterViewport.Width / rasterViewport.Height;
    const Vector3 right = camera.GetRightVec();
    const Vector3 up = camera.GetUpVec();
    const Vector3 topLeftCorner = camera.GetForwardVec() - right * halfScreenWidth + up * halfScreenHeight;

    CameraRays rays;
    rays.origin = camera.GetPosition();
    rays.pixelStepRight = right * (2.0f * halfScreenWidth / rasterViewport.Width);
    rays.pixelStepDown = up * (-2.0f * halfScreenHeight / rasterViewport.Height);
    // raster는 viewport를 TopLeftX, TopLeftY만큼 옮겨 그리므로, render target의 pixel 중심에 있는 sample 위치는 옮기기 전의 화면에서 그만큼 왼쪽 위에 있다.
    rays.topLeftPixelDirection = topLeftCorner
        + rays.pixelStepRight * (0.5f - rasterViewport.TopLeftX)
        + rays.pixelStepDown * (0.5f - rasterViewport.TopLeftY);
    return rays;
}
