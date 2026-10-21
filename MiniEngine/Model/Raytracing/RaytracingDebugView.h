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
    // RaytracingDebugViewCS.hlsl의 View와 같은 값이다.
    enum View
    {
        kHitT, kInstance, kFrontFace,
        kGeometryNormal, kShadingNormal, kUV0, kBaseColor, kMetallic, kRoughness, kEmissive,
        kSunVisibility, kSunCullingDifference,
        kNumViews
    };
    const char* GetViewLabel(View view);

    void Initialize(void);
    // cullSingleSidedBackFaces를 끄면 single-sided material의 뒷면도 ray에 맞아서 culling이 ray에 주는 차이를 볼 수 있다.
    // camera ray는 rasterViewport로 그린 raster의 sample 위치를 지난다. sunDirection은 surface에서 sun을 향하는 방향이다.
    void Render(ComputeContext& context, View view, bool cullSingleSidedBackFaces, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        const RaytracingScene& scene, const Math::Vector3& sunDirection, ColorBuffer& target);
}
