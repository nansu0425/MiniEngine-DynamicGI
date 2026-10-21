#pragma once

class ComputeContext;
class ColorBuffer;
class RaytracingScene;
struct D3D12_VIEWPORT;

namespace Math
{
    class Camera;
    class Vector3;
}

// pixel마다 camera ray를 쏴서 처음 맞은 곳의 hit 정보나 HitSurface, 그곳에서 sun이 보이는지 중 고른 것을 색으로 그린다.
namespace RaytracingDebugView
{
    void Initialize(void);
    // 고른 view가 없으면 아무것도 그리지 않는다. camera ray는 rasterViewport로 그린 raster의 sample 위치를 지난다.
    // sunDirection은 surface에서 sun을 향하는 방향이다.
    void Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        const RaytracingScene& scene, const Math::Vector3& sunDirection, ColorBuffer& target);
}
