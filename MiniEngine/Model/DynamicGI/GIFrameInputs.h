#pragma once

#include "Core/pch.h"

class RaytracingScene;
class ShadowBuffer;

namespace Math
{
    class Camera;
}

// 한 frame의 간접광을 정하는 입력
struct GIFrameInputs
{
    const Math::Camera& camera;
    // raster가 scene을 그린 viewport. GI surface는 이 viewport로 그린 raster의 sample 위치를 쏜다.
    const D3D12_VIEWPORT& rasterViewport;
    const RaytracingScene& scene;
    // surface에서 directional light를 향하는 단위 vector와, light에 수직인 면의 illuminance (lux)
    Math::Vector3 lightDirection;
    Math::Vector3 lightIlluminance;
    // ray가 아무 면도 만나지 않을 때 받는 sky의 radiance (cd/m²). 모든 방향에서 같다.
    Math::Vector3 skyRadiance;
    // raster가 이번 frame에 그린 directional light의 shadow map과, world position을 그 uv와 depth로 옮기는 matrix
    ShadowBuffer& shadowMap;
    Math::Matrix4 shadowMatrix;
};
