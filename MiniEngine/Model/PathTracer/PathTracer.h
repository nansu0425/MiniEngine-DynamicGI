#pragma once

#include <cstdint>

class ComputeContext;
class ColorBuffer;
class RaytracingScene;
struct D3D12_VIEWPORT;

namespace Math
{
    class Camera;
}

// 다른 기능의 정답 image를 만드는 path tracer. pixel마다 sample을 frame에 걸쳐 누적하고 그 평균을 scene color에 쓴다.
// 누적한 평균이 서로 다른 image의 sample을 섞지 않도록, image를 정하는 조건이 쌓을 때와 달라지면 처음부터 다시 쌓는다.
namespace PathTracer
{
    struct Settings
    {
        // pixel마다 한 frame에 더하는 sample 수
        uint32_t samplesPerFrame = 1;
        // 이만큼 쌓으면 trace를 멈추고 평균만 보인다.
        uint32_t targetSamples = 4096;
        // seed만 다른 두 누적은 같은 image의 서로 다른 추정이다.
        uint32_t seed = 0;
    };

    struct Progress
    {
        // reset 뒤 pixel마다 dispatch한 sample 수
        uint32_t samples;
        // reset 뒤 sample을 더한 frame들의 시간
        double seconds;
        // 실행 뒤 처음부터 다시 쌓기 시작한 횟수. 첫 누적도 센다.
        uint32_t resets;
    };

    void Initialize(void);
    void Shutdown(void);

    // 다음 Render가 처음부터 다시 쌓는다.
    void Reset(void);
    Progress GetProgress(void);

    // camera ray는 rasterViewport로 그린 raster의 pixel 격자를 따른다. sceneColor는 raster가 그린 scene이고, 그 크기로 누적해 그 위에 평균을 쓴다.
    void Render(ComputeContext& context, const Settings& settings, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        const RaytracingScene& scene, ColorBuffer& sceneColor);
}
