#pragma once

class DepthBuffer;
class RaytracingScene;
struct D3D12_VIEWPORT;

namespace Math
{
    class Camera;
}

// harness가 client pixel에 무엇이 보이는지 text로 읽는 pick 명령. 그 pixel을 지나는 camera ray를 ray tracing scene에 쏘고,
// 같은 pixel의 raster depth도 읽어 두 결과를 견주게 한다.
namespace RayPick
{
    void Initialize(void);
    void Shutdown(void);
    // 받은 pick이 있을 때만 GPU를 기다려 그 결과로 응답한다. raster가 이 frame의 depth를 다 그리고 그 context를 Finish한 뒤에 부른다.
    // camera ray는 rasterViewport로 그린 raster의 sample 위치를 지나고, rasterDepth는 그 raster가 그린 depth buffer다.
    void Render(const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport, DepthBuffer& rasterDepth, const RaytracingScene& scene);
}
