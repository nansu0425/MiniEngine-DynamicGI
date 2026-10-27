#pragma once

#include "Core/Automation/ControlChannel.h"

class CommandContext;
class PixelBuffer;

// frame이 그려지는 중간의 buffer를 읽기 전용으로 복사해 파일로 쓴다. 쓰는 frame에서만 GPU를 기다린다.
namespace FrameCapture
{
    enum Target
    {
        // tone mapping 전의 HDR scene. 값을 그대로 남기려고 R32G32B32A32_FLOAT DDS로 쓴다.
        kScene,
        // debug view와 heatmap을 그린 scene overlay. PNG로 쓴다.
        kOverlay,
        // UI까지 합성해 present하는 backbuffer. PNG로 쓴다.
        kDisplay,
        // path tracer의 누적 buffer. scene에 쓰기 전의 값을 그대로 남기려고 R32G32B32A32_FLOAT DDS로 쓴다. 채널의 뜻은 PathTracerCS.hlsl에 있다.
        kPathTracer,
        kNumTargets
    };

    // 이름이 없으면 false를 돌려준다.
    bool FindTarget(const std::string& name, Target& target);

    // target이 다음에 다 그려졌을 때 pathWithoutExtension에 확장자를 붙여 쓰고, reply로 그 경로와 크기를 돌려준다.
    // 옆에는 그 frame의 state와, 조건을 되돌릴 수 있으면 그 조건의 줄 (MeasurementConditions)을 남긴다.
    void Request(Target target, const std::string& pathWithoutExtension, const ControlChannel::Reply& reply);

    // target이 다 그려진 곳에서 부른다. 요청이 없으면 아무것도 하지 않는다.
    // buffer는 copy source state로 남는다. context는 state를 추적하므로 다음에 쓰는 곳이 바꾼다.
    void Capture(Target target, PixelBuffer& buffer);
    // context에 기록한 것을 GPU에서 먼저 실행해야 buffer가 다 그려지는 곳에서 쓴다. context는 계속 기록할 수 있다.
    void Capture(Target target, CommandContext& context, PixelBuffer& buffer);
}
