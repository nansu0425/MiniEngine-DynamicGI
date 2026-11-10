#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"

class ColorBuffer;
class ComputeContext;

// G-buffer의 surface가 camera 쪽으로 반사하는 간접광을 path tracing으로 구한다. pixel마다 sample을 frame에 걸쳐 누적한다.
// 누적한 평균이 서로 다른 image의 sample을 섞지 않도록, image를 정하는 조건이 쌓을 때와 달라지면 처음부터 다시 쌓는다.
namespace PathTracedLighting
{
    struct Settings
    {
        // pixel마다 한 frame에 더하는 sample 수
        uint32_t samplesPerFrame = 1;
        // 이만큼 쌓으면 trace를 멈추고 평균만 보인다.
        uint32_t targetSamples = 4096;
        // seed만 다른 두 누적은 같은 image의 서로 다른 추정이다.
        uint32_t seed = 0;
        // G-buffer의 surface에서 나간 path가 맞는 점의 수의 상한. 1이면 DDGI의 single bounce와 같은 빛이다.
        uint32_t maxHits = 16;
        // PathTracing.hlsli의 kFirstRussianRouletteHit번째 hit부터 throughput에 따라 path를 끊고 살아남은 path를 그만큼 키운다.
        // 기대값은 그대로다.
        bool russianRoulette = true;
    };

    struct Progress
    {
        // reset 뒤 pixel마다 dispatch한 sample 수
        uint32_t samples;
        // 실행 뒤 처음부터 다시 쌓기 시작한 횟수. 첫 누적도 센다.
        uint32_t resets;
    };

    void Initialize(void);
    void Shutdown(void);

    // 다음 Accumulate가 처음부터 다시 쌓는다. scene의 model이 바뀌는 것처럼 GIFrameInputs에 드러나지 않는 변경에 부른다.
    void Reset(void);
    Progress GetProgress(void);

    // 이번 frame의 G-buffer를 읽는다. width와 height는 G-buffer의 pixel 격자의 크기다.
    void Accumulate(ComputeContext& context, const Settings& settings, const GIFrameInputs& inputs, uint32_t width, uint32_t height);

    // camera 쪽으로 반사하는 간접광의 radiance (cd/m²). rgb는 평균이고 a는 더한 sample 수다.
    ColorBuffer& GetIndirectRadiance(void);
}
