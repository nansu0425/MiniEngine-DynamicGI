#include "Model/Raytracing/CameraRays.h"
#include "Core/Camera/Camera.h"

using namespace Math;

CameraRays DescribeCameraRays(const Camera& camera, const D3D12_VIEWPORT& rasterViewport)
{
    const float halfScreenHeight = tanf(camera.GetFOV() * 0.5f);
    const float halfScreenWidth = halfScreenHeight * rasterViewport.Width / rasterViewport.Height;
    const Vector3 right = camera.GetRightVec();
    const Vector3 up = camera.GetUpVec();
    const Vector3 frustumTopLeftCorner = camera.GetForwardVec() - right * halfScreenWidth + up * halfScreenHeight;

    CameraRays rays;
    rays.origin = camera.GetPosition();
    rays.pixelStepRight = right * (2.0f * halfScreenWidth / rasterViewport.Width);
    rays.pixelStepDown = up * (-2.0f * halfScreenHeight / rasterViewport.Height);
    // raster는 viewport를 TopLeftX, TopLeftY만큼 옮겨 그리므로, raster pixel의 sample 위치는 옮기지 않은 frustum에서 같은 pixel의 중심보다 그만큼 왼쪽 위에 있다.
    // pixel 격자는 frustum을 TAA를 끈 TemporalEffects의 jitter인 0.5만큼 왼쪽 위로 옮긴 것이므로, sample 위치는 pixel 격자에서 그 pixel의 왼쪽 위 corner부터 (1 - TopLeftX, 1 - TopLeftY)에 있다.
    rays.topLeftCornerDirection = frustumTopLeftCorner - rays.pixelStepRight * 0.5f - rays.pixelStepDown * 0.5f;
    rays.rasterSampleOffset = XMFLOAT2(1.0f - rasterViewport.TopLeftX, 1.0f - rasterViewport.TopLeftY);
    return rays;
}
