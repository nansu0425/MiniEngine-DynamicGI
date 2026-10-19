//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#pragma once

#include <cstdint>

namespace Display
{
    void Initialize(void);
    void Shutdown(void);
    void Resize(uint32_t width, uint32_t height);
    // scene을 그릴 swapchain 영역을 정한다. 크기가 바뀌면 rendering buffer를 그 크기로 다시 만든다.
    void SetSceneViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void Present(void);
}

namespace Graphics
{
    extern uint32_t g_DisplayWidth;
    extern uint32_t g_DisplayHeight;
    extern bool g_bEnableHDROutput;

    // Returns the number of elapsed frames since application start
    uint64_t GetFrameCount(void);

    // The amount of time elapsed during the last completed frame.  The CPU and/or
    // GPU may be idle during parts of the frame.  The frame time measures the time
    // between calls to present each frame.
    float GetFrameTime(void);

    // The total number of frames per second
    float GetFrameRate(void);

    // 직전 frame에서 앞 frame의 Present가 끝난 뒤부터 이번 Present를 호출하기 직전까지 걸린 CPU 시간(초).
    // Present 안에서 기다린 시간과 GetCpuWaitTime은 들어가지 않는다.
    float GetCpuBusyTime(void);

    // 직전 frame에서 CPU가 GPU fence를 기다리며 막혀 있던 시간(초).
    float GetCpuWaitTime(void);

    // 직전 frame의 Present가 끝난 시점과 그 앞 frame의 Present가 끝난 시점 사이의 실측 간격(초).
    // GetFrameTime과 달리 VSync를 켜도 고정값으로 바뀌지 않는다.
    float GetMeasuredFrameTime(void);

    extern bool g_bEnableHDROutput;
}
