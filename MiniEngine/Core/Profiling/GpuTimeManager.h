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

#include "Core/App/GameCore.h"

class CommandContext;

namespace GpuTimeManager
{
    void Initialize( uint32_t MaxNumTimers = 4096 );
    void Shutdown();

    // Reserve a unique timer index
    uint32_t NewTimer(void);

    // Write start and stop time stamps on the GPU timeline
    void StartTimer(CommandContext& Context, uint32_t TimerIdx);
    void StopTimer(CommandContext& Context, uint32_t TimerIdx);

    // Bookend all calls to GetTime() with Begin/End which correspond to Map/Unmap.  This
    // needs to happen either at the very start or very end of a frame.
    void BeginReadBack(void);
    void EndReadBack(void);

    // Returns the time in milliseconds between start and stop queries
    float GetTime(uint32_t TimerIdx);

    // span은 command list 하나가 GPU에서 실행된 구간이다.
    constexpr uint32_t kInvalidSpan = 0xFFFFFFFF;
    uint32_t BeginSpan(CommandContext& Context);
    void EndSpan(CommandContext& Context, uint32_t Span);

    // 한 frame의 span들의 합집합 길이(초). GetTime처럼 BeginReadBack과 EndReadBack 사이에서 부른다.
    float GetBusyTime(void);
}
