#include "Model/Raytracing/CameraRays.h"
#include "Core/Camera/Camera.h"

using namespace Math;

CameraRays DescribeCameraRays(const Camera& camera, const D3D12_VIEWPORT& rasterViewport)
{
    const float halfScreenHeight = tanf(camera.GetFOV() * 0.5f);
    const float halfScreenWidth = halfScreenHeight * rasterViewport.Width / rasterViewport.Height;
    const Vector3 right = camera.GetRightVec();
    const Vector3 up = camera.GetUpVec();

    CameraRays rays;
    rays.origin = camera.GetPosition();
    rays.viewportCornerDirection = camera.GetForwardVec() - right * halfScreenWidth + up * halfScreenHeight;
    rays.pixelStepRight = right * (2.0f * halfScreenWidth / rasterViewport.Width);
    rays.pixelStepDown = up * (-2.0f * halfScreenHeight / rasterViewport.Height);
    rays.viewportOffset = XMFLOAT2(rasterViewport.TopLeftX, rasterViewport.TopLeftY);
    return rays;
}
