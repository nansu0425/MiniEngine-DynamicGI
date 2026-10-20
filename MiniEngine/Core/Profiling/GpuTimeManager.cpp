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

#include "Core/pch.h"
#include "Core/Profiling/GpuTimeManager.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include <algorithm>
#include <atomic>

namespace
{
    ID3D12QueryHeap* sm_QueryHeap = nullptr;
    ID3D12Resource* sm_ReadBackBuffer = nullptr;
    uint64_t* sm_TimeStampBuffer = nullptr;
    uint64_t sm_Fence = 0;
    uint32_t sm_MaxNumTimers = 0;
    uint32_t sm_NumTimers = 1;
    uint64_t sm_ValidTimeStart = 0;
    uint64_t sm_ValidTimeEnd = 0;
    double sm_GpuTickDelta = 0.0;

    // resolve를 기록하는 context도 span을 남긴다. 영역을 frame마다 바꿔야 그 span이 아직 resolve하지 않은
    // 이전 frame의 span을 덮어쓰지 않는다.
    constexpr uint32_t kMaxSpansPerFrame = 1024;
    uint32_t sm_SpanRegionStart[2] = {};
    uint32_t sm_CurrentSpanRegion = 0;
    std::atomic<uint32_t> sm_NumSpans = 0;
    uint32_t sm_ResolvedSpanStart = 0;
    uint32_t sm_NumResolvedSpans = 0;
}

void GpuTimeManager::Initialize(uint32_t MaxNumTimers)
{
    uint64_t GpuFrequency;
    Graphics::g_CommandManager.GetCommandQueue()->GetTimestampFrequency(&GpuFrequency);
    sm_GpuTickDelta = 1.0 / static_cast<double>(GpuFrequency);

    sm_SpanRegionStart[0] = MaxNumTimers * 2;
    sm_SpanRegionStart[1] = sm_SpanRegionStart[0] + kMaxSpansPerFrame * 2;
    const uint32_t NumQueries = sm_SpanRegionStart[1] + kMaxSpansPerFrame * 2;

    D3D12_HEAP_PROPERTIES HeapProps;
    HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
    HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProps.CreationNodeMask = 1;
    HeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC BufferDesc;
    BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufferDesc.Alignment = 0;
    BufferDesc.Width = sizeof(uint64_t) * NumQueries;
    BufferDesc.Height = 1;
    BufferDesc.DepthOrArraySize = 1;
    BufferDesc.MipLevels = 1;
    BufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    BufferDesc.SampleDesc.Count = 1;
    BufferDesc.SampleDesc.Quality = 0;
    BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ASSERT_SUCCEEDED(Graphics::g_Device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE, &BufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, MY_IID_PPV_ARGS(&sm_ReadBackBuffer) ));
    sm_ReadBackBuffer->SetName(L"GpuTimeStamp Buffer");

    D3D12_QUERY_HEAP_DESC QueryHeapDesc;
    QueryHeapDesc.Count = NumQueries;
    QueryHeapDesc.NodeMask = 1;
    QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    ASSERT_SUCCEEDED(Graphics::g_Device->CreateQueryHeap(&QueryHeapDesc, MY_IID_PPV_ARGS(&sm_QueryHeap)));
    sm_QueryHeap->SetName(L"GpuTimeStamp QueryHeap");

    sm_MaxNumTimers = (uint32_t)MaxNumTimers;
}

void GpuTimeManager::Shutdown()
{
    if (sm_ReadBackBuffer != nullptr)
        sm_ReadBackBuffer->Release();

    if (sm_QueryHeap != nullptr)
        sm_QueryHeap->Release();
}

uint32_t GpuTimeManager::NewTimer(void)
{
    return sm_NumTimers++;
}

void GpuTimeManager::StartTimer(CommandContext& Context, uint32_t TimerIdx)
{
    Context.InsertTimeStamp(sm_QueryHeap, TimerIdx * 2);
}

void GpuTimeManager::StopTimer(CommandContext& Context, uint32_t TimerIdx)
{
    Context.InsertTimeStamp(sm_QueryHeap, TimerIdx * 2 + 1);
}

void GpuTimeManager::BeginReadBack(void)
{
    Graphics::g_CommandManager.WaitForFence(sm_Fence);

    D3D12_RANGE Range;
    Range.Begin = 0;
    Range.End = std::max(sm_NumTimers * 2, sm_ResolvedSpanStart + sm_NumResolvedSpans * 2) * sizeof(uint64_t);
    ASSERT_SUCCEEDED(sm_ReadBackBuffer->Map(0, &Range, reinterpret_cast<void**>(&sm_TimeStampBuffer)));

    sm_ValidTimeStart = sm_TimeStampBuffer[0];
    sm_ValidTimeEnd = sm_TimeStampBuffer[1];

    // On the first frame, with random values in the timestamp query heap, we can avoid a misstart.
    if (sm_ValidTimeEnd < sm_ValidTimeStart)
    {
        sm_ValidTimeStart = 0ull;
        sm_ValidTimeEnd = 0ull;
    }
}

void GpuTimeManager::EndReadBack(void)
{
    // Unmap with an empty range to indicate nothing was written by the CPU
    D3D12_RANGE EmptyRange = {};
    sm_ReadBackBuffer->Unmap(0, &EmptyRange);
    sm_TimeStampBuffer = nullptr;

    sm_ResolvedSpanStart = sm_SpanRegionStart[sm_CurrentSpanRegion];
    sm_NumResolvedSpans = std::min(sm_NumSpans.exchange(0), kMaxSpansPerFrame);
    sm_CurrentSpanRegion ^= 1;

    CommandContext& Context = CommandContext::Begin();
    Context.InsertTimeStamp(sm_QueryHeap, 1);
    Context.ResolveTimeStamps(sm_ReadBackBuffer, sm_QueryHeap, 0, sm_NumTimers * 2);
    Context.ResolveTimeStamps(sm_ReadBackBuffer, sm_QueryHeap, sm_ResolvedSpanStart, sm_NumResolvedSpans * 2);
    Context.InsertTimeStamp(sm_QueryHeap, 0);
    sm_Fence = Context.Finish();
}

uint32_t GpuTimeManager::BeginSpan(CommandContext& Context)
{
    // GpuTimeManager보다 먼저 만들어지는 context도 있다.
    if (sm_QueryHeap == nullptr)
        return kInvalidSpan;

    const uint32_t SpanIdx = sm_NumSpans.fetch_add(1);
    if (SpanIdx >= kMaxSpansPerFrame)
        return kInvalidSpan;

    const uint32_t Span = sm_SpanRegionStart[sm_CurrentSpanRegion] + SpanIdx * 2;
    Context.InsertTimeStamp(sm_QueryHeap, Span);
    return Span;
}

void GpuTimeManager::EndSpan(CommandContext& Context, uint32_t Span)
{
    if (Span != kInvalidSpan)
        Context.InsertTimeStamp(sm_QueryHeap, Span + 1);
}

float GpuTimeManager::GetBusyTime(void)
{
    ASSERT(sm_TimeStampBuffer != nullptr, "Time stamp readback buffer is not mapped");

    std::vector<std::pair<uint64_t, uint64_t>> Spans;
    Spans.reserve(sm_NumResolvedSpans);
    for (uint32_t i = 0; i < sm_NumResolvedSpans; ++i)
    {
        const uint64_t Start = sm_TimeStampBuffer[sm_ResolvedSpanStart + i * 2];
        const uint64_t End = sm_TimeStampBuffer[sm_ResolvedSpanStart + i * 2 + 1];
        // resolve 구간 밖의 값은 다른 frame에서 찍혔거나 아직 덮어쓰이지 않은 예전 값이다.
        if (Start >= sm_ValidTimeStart && End <= sm_ValidTimeEnd && Start < End)
            Spans.emplace_back(Start, End);
    }
    std::sort(Spans.begin(), Spans.end());

    uint64_t BusyTicks = 0;
    uint64_t MergedEnd = 0;
    for (const auto& Span : Spans)
    {
        if (Span.second <= MergedEnd)
            continue;
        BusyTicks += Span.second - std::max(Span.first, MergedEnd);
        MergedEnd = Span.second;
    }
    return static_cast<float>(sm_GpuTickDelta * BusyTicks);
}

float GpuTimeManager::GetTime(uint32_t TimerIdx)
{
    ASSERT(sm_TimeStampBuffer != nullptr, "Time stamp readback buffer is not mapped");
    ASSERT(TimerIdx < sm_NumTimers, "Invalid GPU timer index");

    uint64_t TimeStamp1 = sm_TimeStampBuffer[TimerIdx * 2];
    uint64_t TimeStamp2 = sm_TimeStampBuffer[TimerIdx * 2 + 1];

    if (TimeStamp1 < sm_ValidTimeStart || TimeStamp2 > sm_ValidTimeEnd || TimeStamp2 <= TimeStamp1 )
        return 0.0f;

    return static_cast<float>(sm_GpuTickDelta * (TimeStamp2 - TimeStamp1));
}
