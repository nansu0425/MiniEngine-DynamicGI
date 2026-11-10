#pragma once

#include "Core/pch.h"
#include "Core/Math/BoundingBox.h"

class RaytracingScene;
class ShadowBuffer;

namespace Math
{
    class Camera;
}

struct GIFrameInputs
{
    const Math::Camera& camera;
    const D3D12_VIEWPORT& rasterViewport;
    const RaytracingScene& scene;
    Math::Vector3 lightDirection;
    Math::Vector3 lightIlluminance;
    Math::Vector3 skyRadiance;
    ShadowBuffer& shadowMap;
    Math::Matrix4 shadowMatrix;
    Math::AxisAlignedBox modelBounds;
};
