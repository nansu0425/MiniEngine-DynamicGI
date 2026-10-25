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
#include "Core/App/SystemTime.h"
#include "Core/Display/Display.h"
#include "Core/App/GameInput.h"
#include "Core/Profiling/GpuTimeManager.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/UI/ImGuiLayer.h"
#include <imgui.h>
#include <vector>
#include <unordered_map>
#include <array>

using namespace Graphics;
using namespace Math;
using namespace std;

namespace EngineProfiling
{
    bool Paused = false;
}

class StatHistory
{
public:
    StatHistory()
    {
        for (uint32_t i = 0; i < kHistorySize; ++i)
            m_RecentHistory[i] = 0.0f;
        m_Recent = 0.0f;
        m_Average = 0.0f;
        m_Minimum = 0.0f;
        m_Maximum = 0.0f;
    }

    void RecordStat( uint32_t FrameIndex, float Value )
    {
        m_RecentHistory[FrameIndex % kHistorySize] = Value;
        m_Recent = Value;

        uint32_t ValidCount = 0;
        m_Minimum = FLT_MAX;
        m_Maximum = 0.0f;
        m_Average = 0.0f;

        for (float val : m_RecentHistory)
        {
            if (val > 0.0f)
            {
                ++ValidCount;
                m_Average += val;
                m_Minimum = min(val, m_Minimum);
                m_Maximum = max(val, m_Maximum);
            }
        }

        if (ValidCount > 0)
            m_Average /= (float)ValidCount;
        else
            m_Minimum = 0.0f;
    }

    float GetLast(void) const { return m_Recent; }
    float GetMax(void) const { return m_Maximum; }
    float GetMin(void) const { return m_Minimum; }
    float GetAvg(void) const { return m_Average; }

private:
    static const uint32_t kHistorySize = 64;
    float m_RecentHistory[kHistorySize];
    float m_Recent;
    float m_Average;
    float m_Minimum;
    float m_Maximum;
};

class GpuTimer
{
public:

    GpuTimer::GpuTimer()
    {
        m_TimerIndex = GpuTimeManager::NewTimer();
    }

    void Start(CommandContext& Context)
    {
        GpuTimeManager::StartTimer(Context, m_TimerIndex);
    }

    void Stop(CommandContext& Context)
    {
        GpuTimeManager::StopTimer(Context, m_TimerIndex);
    }

    float GpuTimer::GetTime(void)
    {
        return GpuTimeManager::GetTime(m_TimerIndex);
    }

    uint32_t GetTimerIndex(void)
    {
        return m_TimerIndex;
    }
private:

    uint32_t m_TimerIndex;
};

class NestedTimingTree
{
public:
    NestedTimingTree( const wstring& name, NestedTimingTree* parent = nullptr )
        : m_Name(name), m_Parent(parent) {}

    NestedTimingTree* GetChild( const wstring& name )
    {
        auto iter = m_LUT.find(name);
        if (iter != m_LUT.end())
            return iter->second;

        NestedTimingTree* node = new NestedTimingTree(name, this);
        m_Children.push_back(node);
        m_LUT[name] = node;
        return node;
    }

    void StartTiming( CommandContext* Context )
    {
        m_StartTick = SystemTime::GetCurrentTick();
        if (Context == nullptr)
            return;

        m_GpuTimer.Start(*Context);

        Context->PIXBeginEvent(m_Name.c_str());
    }

    void StopTiming( CommandContext* Context )
    {
        m_EndTick = SystemTime::GetCurrentTick();
        if (Context == nullptr)
            return;

        m_GpuTimer.Stop(*Context);

        Context->PIXEndEvent();
    }

    void GatherTimes(uint32_t FrameIndex)
    {
        if (EngineProfiling::Paused)
        {
            for (auto node : m_Children)
                node->GatherTimes(FrameIndex);
            return;
        }
        m_CpuTime.RecordStat(FrameIndex, 1000.0f * (float)SystemTime::TimeBetweenTicks(m_StartTick, m_EndTick));
        m_GpuTime.RecordStat(FrameIndex, 1000.0f * m_GpuTimer.GetTime());

        for (auto node : m_Children)
            node->GatherTimes(FrameIndex);

        m_StartTick = 0;
        m_EndTick = 0;
    }

    static void PushProfilingMarker( const wstring& name, CommandContext* Context );
    static void PopProfilingMarker( CommandContext* Context );
    static void UpdateTimes( void )
    {
        uint32_t FrameIndex = (uint32_t)Graphics::GetFrameCount();

        GpuTimeManager::BeginReadBack();
        sm_RootScope.GatherTimes(FrameIndex);
        s_GpuBusyTime.RecordStat(FrameIndex, 1000.0f * GpuTimeManager::GetBusyTime());
        GpuTimeManager::EndReadBack();

        s_FrameTime.RecordStat(FrameIndex, 1000.0f * Graphics::GetMeasuredFrameTime());
        s_CpuBusyTime.RecordStat(FrameIndex, 1000.0f * Graphics::GetCpuBusyTime());
        s_CpuWaitTime.RecordStat(FrameIndex, 1000.0f * Graphics::GetCpuWaitTime());
    }

    static float GetFrameTime(void) { return s_FrameTime.GetAvg(); }
    static float GetCpuBusyTime(void) { return s_CpuBusyTime.GetAvg(); }
    static float GetCpuWaitTime(void) { return s_CpuWaitTime.GetAvg(); }
    static float GetGpuBusyTime(void) { return s_GpuBusyTime.GetAvg(); }

    static void ShowTree( void );

    static const NestedTimingTree* Find( const wstring& name ) { return sm_RootScope.FindBelow(name); }
    float GetLastCpuTime( void ) const { return m_CpuTime.GetLast(); }
    float GetLastGpuTime( void ) const { return m_GpuTime.GetLast(); }

private:

    const NestedTimingTree* FindBelow( const wstring& name ) const
    {
        for (const NestedTimingTree* node : m_Children)
        {
            if (node->m_Name == name)
                return node;
            if (const NestedTimingTree* found = node->FindBelow(name))
                return found;
        }
        return nullptr;
    }

    void ShowNode( void );
    void DeleteChildren( void )
    {
        for (auto node : m_Children)
            delete node;
        m_Children.clear();
    }

    wstring m_Name;
    NestedTimingTree* m_Parent;
    vector<NestedTimingTree*> m_Children;
    unordered_map<wstring, NestedTimingTree*> m_LUT;
    int64_t m_StartTick;
    int64_t m_EndTick;
    StatHistory m_CpuTime;
    StatHistory m_GpuTime;
    GpuTimer m_GpuTimer;
    static StatHistory s_FrameTime;
    static StatHistory s_CpuBusyTime;
    static StatHistory s_CpuWaitTime;
    static StatHistory s_GpuBusyTime;
    static NestedTimingTree sm_RootScope;
    static NestedTimingTree* sm_CurrentNode;
};

StatHistory NestedTimingTree::s_FrameTime;
StatHistory NestedTimingTree::s_CpuBusyTime;
StatHistory NestedTimingTree::s_CpuWaitTime;
StatHistory NestedTimingTree::s_GpuBusyTime;
NestedTimingTree NestedTimingTree::sm_RootScope(L"");
NestedTimingTree* NestedTimingTree::sm_CurrentNode = &NestedTimingTree::sm_RootScope;
namespace EngineProfiling
{
    BoolVar DrawFrameStats("Profiling/Display Frame Stats", true);
    bool s_ShowProfiler = false;

    void Update( void )
    {
        if (GameInput::IsFirstPressed( GameInput::kStartButton ) 
            || GameInput::IsFirstPressed( GameInput::kKey_space ))
        {
            Paused = !Paused;
        }
        NestedTimingTree::UpdateTimes();
    }

    void BeginBlock(const wstring& name, CommandContext* Context)
    {
        NestedTimingTree::PushProfilingMarker(name, Context);
    }

    void EndBlock(CommandContext* Context)
    {
        NestedTimingTree::PopProfilingMarker(Context);
    }

    bool IsPaused()
    {
        return Paused;
    }

    bool FindLastTimes(const wstring& scope, float& cpuMilliseconds, float& gpuMilliseconds)
    {
        const NestedTimingTree* node = NestedTimingTree::Find(scope);
        if (node == nullptr)
            return false;
        cpuMilliseconds = node->GetLastCpuTime();
        gpuMilliseconds = node->GetLastGpuTime();
        return true;
    }

    void ShowFrameStats( void )
    {
        if (!DrawFrameStats || !ImGuiLayer::BeginSceneOverlay())
            return;

        const float frameTime = NestedTimingTree::GetFrameTime();
        const uint32_t frameRate = frameTime > 0.0f ? (uint32_t)(1000.0f / frameTime + 0.5f) : 0;

        // font의 글자 폭이 제각각이라 공백 대신 가장 긴 label 폭으로 값의 시작 위치를 맞춘다.
        const float valueX = ImGui::GetCursorPosX() + ImGui::CalcTextSize("Frame").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        auto label = [valueX]( const char* text )
        {
            ImGui::TextUnformatted(text);
            ImGui::SameLine(valueX);
        };
        label("Frame");
        ImGui::Text("%.3f ms (%u Hz)", frameTime, frameRate);
        label("CPU");
        ImGui::Text("%.3f ms", NestedTimingTree::GetCpuBusyTime());
        label("GPU");
        ImGui::Text("%.3f ms", NestedTimingTree::GetGpuBusyTime());

        ImGuiLayer::EndSceneOverlay();
    }

    void ShowProfiler( void )
    {
        ImGuiLayer::ShowPanel("Profiler", s_ShowProfiler, []
        {
            ImGui::Checkbox("Pause (Space)", &Paused);
            ImGui::Text("CPU fence wait: %.3f ms", NestedTimingTree::GetCpuWaitTime());
            NestedTimingTree::ShowTree();
        });
    }

} // EngineProfiling

void NestedTimingTree::PushProfilingMarker( const wstring& name, CommandContext* Context )
{
    sm_CurrentNode = sm_CurrentNode->GetChild(name);
    sm_CurrentNode->StartTiming(Context);
}

void NestedTimingTree::PopProfilingMarker( CommandContext* Context )
{
    sm_CurrentNode->StopTiming(Context);
    sm_CurrentNode = sm_CurrentNode->m_Parent;
}

void NestedTimingTree::ShowTree( void )
{
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("##Timings", 3, tableFlags))
    {
        const float timeWidth = ImGui::CalcTextSize("CPU (ms)").x;
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_WidthFixed, timeWidth);
        ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_WidthFixed, timeWidth);
        ImGui::TableHeadersRow();

        for (auto node : sm_RootScope.m_Children)
            node->ShowNode();

        ImGui::EndTable();
    }
}

void NestedTimingTree::ShowNode( void )
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAllColumns;
    if (m_Children.empty())
        nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_Parent == &sm_RootScope)
        nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
    const bool isOpen = ImGui::TreeNodeEx(this, nodeFlags, "%s", Utility::WideStringToUTF8(m_Name).c_str());

    ImGui::TableNextColumn();
    ImGui::Text("%.3f", m_CpuTime.GetAvg());
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", m_GpuTime.GetAvg());

    if (!isOpen || m_Children.empty())
        return;

    for (auto node : m_Children)
        node->ShowNode();
    ImGui::TreePop();
}
