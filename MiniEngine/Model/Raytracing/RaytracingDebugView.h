#pragma once

class ComputeContext;
class ColorBuffer;
class RaytracingScene;

namespace Math
{
    class Camera;
}

// pixel마다 camera ray를 쏴서 처음 맞은 곳의 hit t, instance, front face 중 고른 것을 색으로 그린다.
namespace RaytracingDebugView
{
    void Initialize(void);
    // 고른 view가 없으면 아무것도 그리지 않는다.
    void Render(ComputeContext& context, const Math::Camera& camera, const RaytracingScene& scene, ColorBuffer& target);
}
