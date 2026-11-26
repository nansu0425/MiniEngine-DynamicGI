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

#include <array>
#include <optional>
#include <string>
#include <vector>

class CommandContext;
class GpuResource;

namespace EngineProfiling
{
    constexpr uint32_t kHistoryFrames = 64;

    struct TimeStats
    {
        float last = 0.0f;
        float average = 0.0f;
        float minimum = 0.0f;
        float maximum = 0.0f;
        std::array<float, kHistoryFrames> oldestToNewest = {};
    };

    struct ScopeTimes
    {
        std::wstring name;
        TimeStats cpu;
        TimeStats gpu;
        bool timesGpu = false;
        std::vector<ScopeTimes> children;
    };

    struct FrameTimes
    {
        float frame;
        float cpuBusy;
        float cpuWait;
        float gpuBusy;
    };

    void Update();

    void BeginBlock(const std::wstring& name, CommandContext* Context = nullptr);
    void EndBlock(CommandContext* Context = nullptr);

    void ShowFrameStats(void);
    void ShowProfiler(void);
    bool IsPaused();

    // Profiler의 Scope 열에 보이는 이름이 scope인 timer가 가장 최근에 읽어 들인 시간 (ms). 같은 이름이 여럿이면 tree에서 먼저 만나는 것이다.
    // CPU time은 지난 frame의 것이고, GPU time은 GPU에서 읽어 오는 만큼 그보다 앞 frame의 것이다. 그런 scope가 없으면 false다.
    bool FindLastTimes(const std::wstring& scope, float& cpuMilliseconds, float& gpuMilliseconds);

    std::optional<ScopeTimes> FindScopeTimes(const std::wstring& scope);
    FrameTimes GetAverageFrameTimes(void);
    void SetPaused(bool paused);

    void NoteResourceUse(const GpuResource& resource);
    std::vector<std::wstring> FindScopesUsing(const GpuResource& resource);
}

#ifdef RELEASE
class ScopedTimer
{
public:
    ScopedTimer(const std::wstring&) {}
    ScopedTimer(const std::wstring&, CommandContext&) {}
};
#else
class ScopedTimer
{
public:
    ScopedTimer( const std::wstring& name ) : m_Context(nullptr)
    {
        EngineProfiling::BeginBlock(name);
    }
    ScopedTimer( const std::wstring& name, CommandContext& Context ) : m_Context(&Context)
    {
        EngineProfiling::BeginBlock(name, m_Context);
    }
    ~ScopedTimer()
    {
        EngineProfiling::EndBlock(m_Context);
    }

private:
    CommandContext* m_Context;
};
#endif
