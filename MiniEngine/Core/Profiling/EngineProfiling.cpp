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
#include "Core/UI/TextRenderer.h"
#include "Core/Profiling/GraphRenderer.h"
#include "Core/App/GameInput.h"
#include "Core/Profiling/GpuTimeManager.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/UI/ImGuiLayer.h"
#include <imgui.h>
#include <vector>
#include <unordered_map>
#include <array>

using namespace Graphics;
using namespace GraphRenderer;
using namespace Math;
using namespace std;

#define PERF_GRAPH_ERROR uint32_t(0xFFFFFFFF)
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
        for (uint32_t i = 0; i < kExtendedHistorySize; ++i)
            m_ExtendedHistory[i] = 0.0f;
        m_Average = 0.0f;
        m_Minimum = 0.0f;
        m_Maximum = 0.0f;
    }

    void RecordStat( uint32_t FrameIndex, float Value )
    {
        m_RecentHistory[FrameIndex % kHistorySize] = Value;
        m_ExtendedHistory[FrameIndex % kExtendedHistorySize] = Value;
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

    const float* GetHistory(void) const { return m_ExtendedHistory; }
    uint32_t GetHistoryLength(void) const { return kExtendedHistorySize; }

private:
    static const uint32_t kHistorySize = 64;
    static const uint32_t kExtendedHistorySize = 256;
    float m_RecentHistory[kHistorySize];
    float m_ExtendedHistory[kExtendedHistorySize];
    float m_Recent;
    float m_Average;
    float m_Minimum;
    float m_Maximum;
};

class StatPlot
{
public:
    StatPlot(StatHistory& Data, Color Col = Color(1.0f, 1.0f, 1.0f))
        : m_StatData(Data), m_PlotColor(Col)
    {
    }

    void SetColor( Color Col )
    {
        m_PlotColor = Col;
    }

private:
    StatHistory& m_StatData;
    Color m_PlotColor;
};

class StatGraph
{
public:
    StatGraph(const wstring& Label, D3D12_RECT Window)
        : m_Label(Label), m_Window(Window), m_BGColor(0.0f, 0.0f, 0.0f, 0.2f)
    {
    }

    void SetLabel(const wstring& Label)
    {
        m_Label = Label;
    }

    void SetWindow(D3D12_RECT Window)
    {
        m_Window = Window;
    }

    uint32_t AddPlot( const StatPlot& P )
    {
        uint32_t Idx = (uint32_t)m_Stats.size();
        m_Stats.push_back(P);
        return Idx;
    }

    StatPlot& GetPlot( uint32_t Handle );

    void Draw( GraphicsContext& Context );

private:
    wstring m_Label;
    D3D12_RECT m_Window;
    vector<StatPlot> m_Stats;
    Color m_BGColor;
    float m_PeakValue;
};

class GraphManager
{
public:

private:
    vector<StatGraph> m_Graphs;
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
        : m_Name(name), m_Parent(parent), m_IsGraphed(false), m_GraphHandle(PERF_GRAPH_ERROR) {}

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

    void SumInclusiveTimes(float& cpuTime, float& gpuTime)
    {
        cpuTime = 0.0f;
        gpuTime = 0.0f;
        for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
        {
            cpuTime += (*iter)->m_CpuTime.GetLast();
            gpuTime += (*iter)->m_GpuTime.GetLast();
        }
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

        float TotalCpuTime, TotalGpuTime;
        sm_RootScope.SumInclusiveTimes(TotalCpuTime, TotalGpuTime);
        s_TotalCpuTime.RecordStat(FrameIndex, TotalCpuTime);
        s_TotalGpuTime.RecordStat(FrameIndex, TotalGpuTime);

        GraphRenderer::Update(XMFLOAT2(TotalCpuTime, TotalGpuTime), 0, GraphType::Global);
    }

    // 최근 frame들의 평균(ms)
    static float GetFrameTime(void) { return s_FrameTime.GetAvg(); }
    static float GetCpuBusyTime(void) { return s_CpuBusyTime.GetAvg(); }
    static float GetCpuWaitTime(void) { return s_CpuWaitTime.GetAvg(); }
    static float GetGpuBusyTime(void) { return s_GpuBusyTime.GetAvg(); }

    static void ShowTree( void );

    bool IsGraphed(){ return m_IsGraphed;}

private:

    void ShowNode( void );
    void StoreToGraph(void);
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
    bool m_IsGraphed;
    GraphHandle m_GraphHandle;
    static StatHistory s_TotalCpuTime;
    static StatHistory s_TotalGpuTime;
    static StatHistory s_FrameTime;
    static StatHistory s_CpuBusyTime;
    static StatHistory s_CpuWaitTime;
    static StatHistory s_GpuBusyTime;
    static NestedTimingTree sm_RootScope;
    static NestedTimingTree* sm_CurrentNode;
};

StatHistory NestedTimingTree::s_TotalCpuTime;
StatHistory NestedTimingTree::s_TotalGpuTime;
StatHistory NestedTimingTree::s_FrameTime;
StatHistory NestedTimingTree::s_CpuBusyTime;
StatHistory NestedTimingTree::s_CpuWaitTime;
StatHistory NestedTimingTree::s_GpuBusyTime;
NestedTimingTree NestedTimingTree::sm_RootScope(L"");
NestedTimingTree* NestedTimingTree::sm_CurrentNode = &NestedTimingTree::sm_RootScope;
namespace EngineProfiling
{
    BoolVar DrawFrameStats("Display Frame Stats", true);
    bool s_ShowProfiler = false;
    //BoolVar DrawPerfGraph("Display Performance Graph", false);
    const bool DrawPerfGraph = false;
    
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

    void DisplayPerfGraph( GraphicsContext& Context )
    {
        if (DrawPerfGraph)
            GraphRenderer::RenderGraphs(Context, GraphType::Global );
    }

    void ShowProfiler( void )
    {
        ImGuiLayer::ShowPanel("Profiler", s_ShowProfiler, []
        {
            ImGui::Checkbox("Pause (Space)", &Paused);
            // overlay의 CPU busy에서 뺀 시간이다.
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

    sm_RootScope.StoreToGraph();
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

void NestedTimingTree::StoreToGraph(void)
{
    if (m_GraphHandle != PERF_GRAPH_ERROR)
        GraphRenderer::Update( XMFLOAT2(m_CpuTime.GetLast(), m_GpuTime.GetLast()), m_GraphHandle, GraphType::Profile);

    for (auto node : m_Children)
        node->StoreToGraph();
}
