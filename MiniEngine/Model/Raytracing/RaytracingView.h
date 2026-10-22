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

// 고른 view 하나를 scene overlay에 그린다. 모든 view가 같은 overlay에 그리므로 그룹이 달라도 view를 하나만 고르게 한다.
namespace RaytracingView
{
    void Initialize(void);
    void Shutdown(void);
    // Ray Tracing panel의 View section 내용. view를 고르고 고른 view의 설정을 바꾼다.
    void ShowSection(void);
    // 고른 view가 없으면 아무것도 그리지 않는다. 인자의 뜻은 RaytracingDebugView::Render와 같다.
    void Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        const RaytracingScene& scene, const Math::Vector3& sunDirection, ColorBuffer& target);
}
