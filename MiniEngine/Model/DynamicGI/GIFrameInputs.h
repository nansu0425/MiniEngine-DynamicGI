#pragma once

#include "Core/pch.h"
#include "Model/Lights/SceneLights.h"

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
    SceneLights lights;
    ShadowBuffer& shadowMap;
    Math::Matrix4 shadowMatrix;
};
