#include "Model/DynamicGI/GIProfiler.h"
#include "Core/App/UserSettings.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/Graphics/FormatName.h"
#include "Core/Profiling/EngineProfiling.h"
#include "Core/Profiling/GpuMemory.h"
#include "Core/Scene/SettingValues.h"
#include "Core/UI/ImGuiLayer.h"
#include "Core/Utility/Utility.h"

#include <imgui.h>
#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using EngineProfiling::ScopeTimes;
using EngineProfiling::TimeStats;

namespace
{
    const char kPanelName[] = "Dynamic GI Profiler";
    const char* const kRoleNames[] = { "Probe Data", "Ray Buffer", "Screen", "Debug", "Shared" };
    constexpr size_t kNumRoles = std::size(kRoleNames);
    constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;

    bool s_ShowPanel = false;
    float s_GpuBudgetMilliseconds = 0.0f;
    std::function<GIProfiling::Report(void)> s_DescribeReport;

    struct LastRun
    {
        float cpuMilliseconds = 0.0f;
        float gpuMilliseconds = 0.0f;
    };
    std::unordered_map<std::wstring, LastRun> s_LastRuns;

    struct ProfiledScopes
    {
        std::optional<ScopeTimes> pipeline;
        std::vector<ScopeTimes> debug;
    };

    struct MemoryRow
    {
        GpuMemory::Allocation allocation;
        GIProfiling::ResourceRole role;
        std::vector<std::wstring> users;
    };

    struct MemoryTotals
    {
        uint64_t owned = 0;
        uint64_t byRole[kNumRoles] = {};
    };

    struct PassTable
    {
        const GIProfiling::Workload& workload;
        float pipelineGpuMilliseconds;
    };

    std::string ToUTF8(const std::wstring& text)
    {
        return Utility::WideStringToUTF8(text);
    }

    ProfiledScopes FindProfiledScopes(const GIProfiling::Scopes& scopes)
    {
        ProfiledScopes profiled = { .pipeline = EngineProfiling::FindScopeTimes(scopes.pipeline) };
        for (const wchar_t* scope : scopes.debug)
        {
            if (std::optional<ScopeTimes> times = EngineProfiling::FindScopeTimes(scope))
                profiled.debug.push_back(std::move(*times));
        }
        return profiled;
    }

    bool RanLastFrame(const ScopeTimes& scope)
    {
        return scope.cpu.last > 0.0f;
    }

    float SumGpu(const ScopeTimes& scope, float TimeStats::* stat)
    {
        if (scope.timesGpu)
            return scope.gpu.*stat;
        float sum = 0.0f;
        for (const ScopeTimes& child : scope.children)
            sum += SumGpu(child, stat);
        return sum;
    }

    std::wstring GetScopePath(const std::wstring& parentPath, const ScopeTimes& scope)
    {
        return parentPath + L"/" + scope.name;
    }

    void RecordLastRuns(const ScopeTimes& scope, const std::wstring& parentPath)
    {
        const std::wstring path = GetScopePath(parentPath, scope);
        LastRun& lastRun = s_LastRuns[path];
        if (scope.cpu.last > 0.0f)
            lastRun.cpuMilliseconds = scope.cpu.last;
        if (scope.timesGpu && scope.gpu.last > 0.0f)
            lastRun.gpuMilliseconds = scope.gpu.last;
        for (const ScopeTimes& child : scope.children)
            RecordLastRuns(child, path);
    }

    const GIProfiling::PassUnits* FindPassUnits(const GIProfiling::Workload& workload, const std::wstring& scope)
    {
        for (const GIProfiling::PassUnits& units : workload.passes)
        {
            if (scope == units.scope)
                return units.count > 0 ? &units : nullptr;
        }
        return nullptr;
    }

    double GetNanosecondsPerUnit(float gpuMilliseconds, const GIProfiling::PassUnits& units)
    {
        return gpuMilliseconds * kNanosecondsPerMillisecond / (double)units.count;
    }

    std::vector<MemoryRow> DescribeMemory(const std::vector<GIProfiling::Resource>& resources)
    {
        std::vector<MemoryRow> rows;
        for (const GIProfiling::Resource& resource : resources)
        {
            GpuMemory::Allocation allocation = GpuMemory::DescribeAllocation(*resource.resource);
            if (allocation.bytes == 0)
                continue;
            if (allocation.name.empty())
                allocation.name = L"(unnamed)";
            rows.push_back({ std::move(allocation), resource.role, EngineProfiling::FindScopesUsing(*resource.resource) });
        }
        std::stable_sort(rows.begin(), rows.end(), [](const MemoryRow& a, const MemoryRow& b)
        {
            if (a.role != b.role)
                return a.role < b.role;
            return a.allocation.bytes > b.allocation.bytes;
        });
        return rows;
    }

    MemoryTotals SumMemory(const std::vector<MemoryRow>& rows)
    {
        MemoryTotals totals;
        for (const MemoryRow& row : rows)
        {
            totals.byRole[(size_t)row.role] += row.allocation.bytes;
            if (row.role != GIProfiling::ResourceRole::kShared)
                totals.owned += row.allocation.bytes;
        }
        return totals;
    }

    double ToMegabytes(uint64_t bytes)
    {
        return bytes / kBytesPerMegabyte;
    }

    bool IsBuffer(const D3D12_RESOURCE_DESC& desc)
    {
        return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    }

    std::string FormatSize(const D3D12_RESOURCE_DESC& desc)
    {
        if (IsBuffer(desc))
            return std::format("{} B", desc.Width);
        if (desc.DepthOrArraySize > 1)
            return std::format("{} x {} x {}", desc.Width, desc.Height, desc.DepthOrArraySize);
        return std::format("{} x {}", desc.Width, desc.Height);
    }

    const char* FormatElement(const D3D12_RESOURCE_DESC& desc)
    {
        return IsBuffer(desc) ? "Buffer" : Graphics::GetFormatName(desc.Format);
    }

    std::string JoinScopes(const std::vector<std::wstring>& scopes)
    {
        std::string joined;
        for (const std::wstring& scope : scopes)
            joined += (joined.empty() ? "" : ", ") + ToUTF8(scope);
        return joined;
    }

    void ShowMilliseconds(float milliseconds)
    {
        if (milliseconds > 0.0f)
            ImGui::Text("%.3f", milliseconds);
        else
            ImGui::TextDisabled("-");
    }

    void ShowScopeRow(const ScopeTimes& scope, const std::wstring& parentPath, const PassTable& table)
    {
        const std::wstring path = GetScopePath(parentPath, scope);
        const LastRun& lastRun = s_LastRuns[path];
        const float gpuAverage = SumGpu(scope, &TimeStats::average);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;
        if (scope.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        const bool ran = RanLastFrame(scope);
        if (!ran)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const std::string pathText = ToUTF8(path);
        const bool isOpen = ImGui::TreeNodeEx(pathText.c_str(), flags, "%s", ToUTF8(scope.name).c_str());

        ImGui::TableNextColumn();
        ShowMilliseconds(gpuAverage);
        ImGui::TableNextColumn();
        ShowMilliseconds(scope.timesGpu ? scope.gpu.maximum : 0.0f);
        ImGui::TableNextColumn();
        ShowMilliseconds(lastRun.gpuMilliseconds);
        ImGui::TableNextColumn();
        ShowMilliseconds(scope.cpu.average);
        ImGui::TableNextColumn();
        ShowMilliseconds(lastRun.cpuMilliseconds);
        ImGui::TableNextColumn();
        if (gpuAverage > 0.0f && table.pipelineGpuMilliseconds > 0.0f)
            ImGui::Text("%.1f%%", 100.0f * gpuAverage / table.pipelineGpuMilliseconds);
        ImGui::TableNextColumn();
        if (const GIProfiling::PassUnits* units = FindPassUnits(table.workload, scope.name); units != nullptr && gpuAverage > 0.0f)
            ImGui::Text("%.3g ns/%s", GetNanosecondsPerUnit(gpuAverage, *units), units->unit);

        if (!ran)
            ImGui::PopStyleColor();

        if (!isOpen || scope.children.empty())
            return;
        for (const ScopeTimes& child : scope.children)
            ShowScopeRow(child, path, table);
        ImGui::TreePop();
    }

    void ShowScopeTable(const char* id, const std::vector<const ScopeTimes*>& scopes, const PassTable& table)
    {
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg;
        if (!ImGui::BeginTable(id, 8, tableFlags))
            return;

        const float timeWidth = ImGui::CalcTextSize("GPU last run").x;
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_NoHide);
        for (const char* column : { "GPU avg", "GPU max", "GPU last run", "CPU avg", "CPU last run", "% GI" })
            ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed, timeWidth);
        ImGui::TableSetupColumn("Per unit", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0.000 ns/sample").x);
        ImGui::TableHeadersRow();

        for (const ScopeTimes* scope : scopes)
            ShowScopeRow(*scope, std::wstring(), table);
        ImGui::EndTable();
    }

    void CollectGpuLeaves(const ScopeTimes& scope, std::vector<const ScopeTimes*>& leaves)
    {
        if (scope.timesGpu && scope.children.empty())
            leaves.push_back(&scope);
        for (const ScopeTimes& child : scope.children)
            CollectGpuLeaves(child, leaves);
    }

    ImU32 GetLeafColor(size_t leaf, size_t leafCount)
    {
        return ImColor::HSV((float)leaf / (float)std::max<size_t>(leafCount, 1), 0.55f, 0.85f);
    }

    void ShowGpuHistory(const ScopeTimes& pipeline)
    {
        std::vector<const ScopeTimes*> leaves;
        CollectGpuLeaves(pipeline, leaves);
        const std::array<float, EngineProfiling::kHistoryFrames>& totals = pipeline.gpu.oldestToNewest;
        const float top = 1.1f * std::max(*std::max_element(totals.begin(), totals.end()), s_GpuBudgetMilliseconds);
        if (top <= 0.0f)
        {
            ImGui::TextDisabled("No GPU time yet.");
            return;
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, 8.0f * ImGui::GetTextLineHeightWithSpacing());
        ImGui::InvisibleButton("##GpuHistory", size);
        const float barWidth = size.x / EngineProfiling::kHistoryFrames;
        const auto toY = [&](float milliseconds) { return origin.y + size.y * (1.0f - milliseconds / top); };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
        for (uint32_t frame = 0; frame < EngineProfiling::kHistoryFrames; ++frame)
        {
            const float left = origin.x + frame * barWidth;
            float stacked = 0.0f;
            for (size_t leaf = 0; leaf < leaves.size(); ++leaf)
            {
                const float milliseconds = leaves[leaf]->gpu.oldestToNewest[frame];
                if (milliseconds <= 0.0f)
                    continue;
                drawList->AddRectFilled(ImVec2(left, toY(stacked + milliseconds)), ImVec2(left + barWidth - 1.0f, toY(stacked)), GetLeafColor(leaf, leaves.size()));
                stacked += milliseconds;
            }
            if (totals[frame] > 0.0f)
                drawList->AddLine(ImVec2(left, toY(totals[frame])), ImVec2(left + barWidth, toY(totals[frame])), ImGui::GetColorU32(ImGuiCol_Text));
        }
        if (s_GpuBudgetMilliseconds > 0.0f)
            drawList->AddLine(ImVec2(origin.x, toY(s_GpuBudgetMilliseconds)), ImVec2(origin.x + size.x, toY(s_GpuBudgetMilliseconds)), IM_COL32(255, 80, 80, 255));

        if (ImGui::IsItemHovered())
        {
            const uint32_t frame = std::min((uint32_t)std::max(0.0f, (ImGui::GetMousePos().x - origin.x) / barWidth), EngineProfiling::kHistoryFrames - 1);
            ImGui::BeginTooltip();
            ImGui::Text("%s: %.3f ms", ToUTF8(pipeline.name).c_str(), totals[frame]);
            for (const ScopeTimes* leaf : leaves)
                ImGui::Text("%s: %.3f ms", ToUTF8(leaf->name).c_str(), leaf->gpu.oldestToNewest[frame]);
            ImGui::EndTooltip();
        }

        for (size_t leaf = 0; leaf < leaves.size(); ++leaf)
        {
            const std::string name = ToUTF8(leaves[leaf]->name);
            const float itemWidth = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(name.c_str()).x;
            if (leaf > 0)
            {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < itemWidth)
                    ImGui::NewLine();
            }
            const ImVec2 swatch = ImGui::GetCursorScreenPos();
            const float swatchSize = ImGui::GetTextLineHeight();
            drawList->AddRectFilled(swatch, ImVec2(swatch.x + swatchSize, swatch.y + swatchSize), GetLeafColor(leaf, leaves.size()));
            ImGui::Dummy(ImVec2(swatchSize, swatchSize));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(name.c_str());
        }
    }

    void ShowMemoryTable(const std::vector<MemoryRow>& rows)
    {
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg;
        if (!ImGui::BeginTable("##Memory", 6, tableFlags))
            return;

        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Probe Data").x);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("R32G32B32A32_FLOAT").x);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0000000000 B").x);
        ImGui::TableSetupColumn("MB", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0000.00").x);
        ImGui::TableSetupColumn("Used by");
        ImGui::TableHeadersRow();

        for (const MemoryRow& row : rows)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ToUTF8(row.allocation.name).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kRoleNames[(size_t)row.role]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatElement(row.allocation.desc));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatSize(row.allocation.desc).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", ToMegabytes(row.allocation.bytes));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(JoinScopes(row.users).c_str());
        }
        ImGui::EndTable();
    }

    float SumDebugGpu(const ProfiledScopes& profiled)
    {
        float sum = 0.0f;
        for (const ScopeTimes& scope : profiled.debug)
            sum += SumGpu(scope, &TimeStats::average);
        return sum;
    }

    float SumDebugCpu(const ProfiledScopes& profiled)
    {
        float sum = 0.0f;
        for (const ScopeTimes& scope : profiled.debug)
            sum += scope.cpu.average;
        return sum;
    }

    float GetPipelineGpu(const ProfiledScopes& profiled)
    {
        return profiled.pipeline ? SumGpu(*profiled.pipeline, &TimeStats::average) : 0.0f;
    }

    void ShowSummary(const ProfiledScopes& profiled, const MemoryTotals& memory, const GIProfiling::Workload& workload)
    {
        const float gpuBusy = EngineProfiling::GetAverageFrameTimes().gpuBusy;
        const float pipelineGpu = GetPipelineGpu(profiled);
        ImGui::Text("GI GPU %.3f ms (%.1f%% of GPU busy %.3f ms)", pipelineGpu, gpuBusy > 0.0f ? 100.0f * pipelineGpu / gpuBusy : 0.0f, gpuBusy);
        if (s_GpuBudgetMilliseconds > 0.0f)
        {
            ImGui::SameLine();
            if (pipelineGpu > s_GpuBudgetMilliseconds)
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "over budget by %.3f ms", pipelineGpu - s_GpuBudgetMilliseconds);
            else
                ImGui::TextDisabled("within budget %.3f ms", s_GpuBudgetMilliseconds);
        }
        ImGui::Text("GI CPU %.3f ms", profiled.pipeline ? profiled.pipeline->cpu.average : 0.0f);
        ImGui::Text("Debug GPU %.3f ms, CPU %.3f ms", SumDebugGpu(profiled), SumDebugCpu(profiled));
        ImGui::Text("Memory %.2f MB, shared %.2f MB", ToMegabytes(memory.owned), ToMegabytes(memory.byRole[(size_t)GIProfiling::ResourceRole::kShared]));
        std::string roles;
        for (size_t role = 0; role < kNumRoles; ++role)
        {
            if (role != (size_t)GIProfiling::ResourceRole::kShared)
                roles += std::format("{}{} {:.2f} MB", roles.empty() ? "" : ", ", kRoleNames[role], ToMegabytes(memory.byRole[role]));
        }
        ImGui::TextDisabled("%s", roles.c_str());
        for (const GIProfiling::Condition& condition : workload.conditions)
            ImGui::TextDisabled("%s: %s", condition.name.c_str(), condition.value.c_str());
    }

    void AppendScopeText(std::string& text, const ScopeTimes& scope, const PassTable& table, int depth)
    {
        const float gpuAverage = SumGpu(scope, &TimeStats::average);
        text += std::format("{}{}\tGPU {:.3f} ms\tCPU {:.3f} ms", std::string(2 * depth, ' '), ToUTF8(scope.name), gpuAverage, scope.cpu.average);
        if (const GIProfiling::PassUnits* units = FindPassUnits(table.workload, scope.name); units != nullptr && gpuAverage > 0.0f)
            text += std::format("\t{:.3g} ns/{}", GetNanosecondsPerUnit(gpuAverage, *units), units->unit);
        text += "\n";
        for (const ScopeTimes& child : scope.children)
            AppendScopeText(text, child, table, depth + 1);
    }

    std::string FormatReportText(const ProfiledScopes& profiled, const std::vector<MemoryRow>& rows, const PassTable& table)
    {
        std::string text;
        for (const GIProfiling::Condition& condition : table.workload.conditions)
            text += std::format("{}: {}\n", condition.name, condition.value);
        text += "\nPasses\n";
        if (profiled.pipeline)
            AppendScopeText(text, *profiled.pipeline, table, 0);
        text += "\nDebug\n";
        for (const ScopeTimes& scope : profiled.debug)
            AppendScopeText(text, scope, table, 0);
        text += "\nMemory\n";
        for (const MemoryRow& row : rows)
        {
            text += std::format("{}\t{}\t{}\t{}\t{:.2f} MB\t{}\n", ToUTF8(row.allocation.name), kRoleNames[(size_t)row.role], FormatElement(row.allocation.desc),
                FormatSize(row.allocation.desc), ToMegabytes(row.allocation.bytes), JoinScopes(row.users));
        }
        return text;
    }

    void ShowPanelContent(const GIProfiling::Report& report, const ProfiledScopes& profiled)
    {
        bool paused = EngineProfiling::IsPaused();
        if (ImGui::Checkbox("Pause (Space)", &paused))
            EngineProfiling::SetPaused(paused);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("000.000").x + 2.0f * ImGui::GetFrameHeight());
        if (ImGui::InputFloat("GPU Budget (ms)", &s_GpuBudgetMilliseconds, 0.1f, 1.0f, "%.3f"))
            s_GpuBudgetMilliseconds = std::max(s_GpuBudgetMilliseconds, 0.0f);

        const std::vector<MemoryRow> rows = DescribeMemory(report.resources);
        const PassTable table = { .workload = report.workload, .pipelineGpuMilliseconds = GetPipelineGpu(profiled) };
        ImGui::SameLine();
        if (ImGui::Button("Copy"))
            ImGui::SetClipboardText(FormatReportText(profiled, rows, table).c_str());

        ShowSummary(profiled, SumMemory(rows), report.workload);

        if (ImGui::CollapsingHeader("Passes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (profiled.pipeline)
                ShowScopeTable("##Passes", { &*profiled.pipeline }, table);
            else
                ImGui::TextDisabled("%s has not run.", ToUTF8(report.scopes.pipeline).c_str());
        }
        if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::vector<const ScopeTimes*> debug;
            for (const ScopeTimes& scope : profiled.debug)
                debug.push_back(&scope);
            ShowScopeTable("##Debug", debug, table);
        }
        if (ImGui::CollapsingHeader("GPU History", ImGuiTreeNodeFlags_DefaultOpen) && profiled.pipeline)
            ShowGpuHistory(*profiled.pipeline);
        if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
            ShowMemoryTable(rows);
    }

    struct Accumulation
    {
        double sum = 0.0;
        float minimum = 0.0f;
        float maximum = 0.0f;
        uint32_t frames = 0;

        void Add(float value)
        {
            minimum = frames == 0 ? value : std::min(minimum, value);
            maximum = frames == 0 ? value : std::max(maximum, value);
            sum += value;
            ++frames;
        }

        float GetMean(void) const
        {
            return frames > 0 ? (float)(sum / frames) : 0.0f;
        }
    };

    struct MeasuredScope
    {
        std::wstring name;
        Accumulation cpu;
        Accumulation gpu;
        std::vector<MeasuredScope> children;
    };

    struct Measurement
    {
        uint32_t frames;
        uint32_t measured;
        GIProfiling::Scopes scopes;
        MeasuredScope pipeline;
        std::vector<MeasuredScope> debug;
        ControlChannel::Reply reply;
    };

    void Accumulate(MeasuredScope& measured, const ScopeTimes& times)
    {
        if (times.cpu.last > 0.0f)
            measured.cpu.Add(times.cpu.last);
        if (times.timesGpu && times.gpu.last > 0.0f)
            measured.gpu.Add(times.gpu.last);
        for (const ScopeTimes& child : times.children)
        {
            auto found = std::find_if(measured.children.begin(), measured.children.end(), [&](const MeasuredScope& c) { return c.name == child.name; });
            if (found == measured.children.end())
            {
                measured.children.push_back({ .name = child.name });
                found = measured.children.end() - 1;
            }
            Accumulate(*found, child);
        }
    }

    ControlChannel::Fields DescribeAccumulation(const Accumulation& accumulation)
    {
        return ControlChannel::Fields()
            .Float("mean", accumulation.GetMean())
            .Float("min", accumulation.minimum)
            .Float("max", accumulation.maximum)
            .Integer("frames", accumulation.frames);
    }

    ControlChannel::Fields DescribeMeasuredScope(const MeasuredScope& scope, const GIProfiling::Workload& workload)
    {
        ControlChannel::Fields fields;
        if (scope.cpu.frames > 0)
            fields.Object("cpu_ms", DescribeAccumulation(scope.cpu));
        if (scope.gpu.frames > 0)
            fields.Object("gpu_ms", DescribeAccumulation(scope.gpu));
        if (const GIProfiling::PassUnits* units = FindPassUnits(workload, scope.name); units != nullptr && scope.gpu.frames > 0)
        {
            fields.Object("units", ControlChannel::Fields()
                .Text("unit", units->unit)
                .Integer("count", (int64_t)units->count)
                .Float("gpu_ns_per_unit", (float)GetNanosecondsPerUnit(scope.gpu.GetMean(), *units)));
        }
        if (!scope.children.empty())
        {
            ControlChannel::Fields children;
            for (const MeasuredScope& child : scope.children)
                children.Object(ToUTF8(child.name), DescribeMeasuredScope(child, workload));
            fields.Object("children", children);
        }
        return fields;
    }

    ControlChannel::Fields DescribeMemoryFields(const std::vector<MemoryRow>& rows)
    {
        const MemoryTotals totals = SumMemory(rows);
        ControlChannel::Fields resources;
        for (const MemoryRow& row : rows)
        {
            resources.Object(ToUTF8(row.allocation.name), ControlChannel::Fields()
                .Text("role", kRoleNames[(size_t)row.role])
                .Text("format", FormatElement(row.allocation.desc))
                .Text("size", FormatSize(row.allocation.desc))
                .Integer("bytes", (int64_t)row.allocation.bytes)
                .Text("used_by", JoinScopes(row.users)));
        }
        return ControlChannel::Fields()
            .Integer("gi_bytes", (int64_t)totals.owned)
            .Integer("shared_bytes", (int64_t)totals.byRole[(size_t)GIProfiling::ResourceRole::kShared])
            .Object("resources", resources);
    }

    ControlChannel::Fields DescribeMeasurement(const Measurement& measurement, const GIProfiling::Report& report)
    {
        ControlChannel::Fields debug;
        for (const MeasuredScope& scope : measurement.debug)
            debug.Object(ToUTF8(scope.name), DescribeMeasuredScope(scope, report.workload));
        ControlChannel::Fields conditions;
        for (const GIProfiling::Condition& condition : report.workload.conditions)
            conditions.Text(condition.name, condition.value);
        return ControlChannel::Fields()
            .Integer("frames", measurement.measured)
            .Object("conditions", conditions)
            .Object("pipeline", ControlChannel::Fields().Object(ToUTF8(measurement.pipeline.name), DescribeMeasuredScope(measurement.pipeline, report.workload)))
            .Object("debug", debug)
            .Object("memory", DescribeMemoryFields(DescribeMemory(report.resources)));
    }

    void MeasureNextFrame(const std::shared_ptr<Measurement>& measurement)
    {
        ControlChannel::AfterFrames(1, [measurement]
        {
            Measurement& m = *measurement;
            if (EngineProfiling::IsPaused())
            {
                m.reply.Error("the profiler was paused while measuring, so its times stopped changing");
                return;
            }
            if (std::optional<ScopeTimes> times = EngineProfiling::FindScopeTimes(m.scopes.pipeline))
                Accumulate(m.pipeline, *times);
            for (size_t i = 0; i < m.scopes.debug.size(); ++i)
            {
                if (std::optional<ScopeTimes> times = EngineProfiling::FindScopeTimes(m.scopes.debug[i]))
                    Accumulate(m.debug[i], *times);
            }
            if (++m.measured < m.frames)
            {
                MeasureNextFrame(measurement);
                return;
            }
            m.reply.Ok(DescribeMeasurement(m, s_DescribeReport()));
        });
    }

    void SaveSettings(UserSettings::Values& values)
    {
        values["GPU Budget"] = MeasurementConditions::FormatFloat(s_GpuBudgetMilliseconds);
    }

    void LoadSettings(const UserSettings::Values& values)
    {
        SettingValues::ReadFloat(values, "GPU Budget", s_GpuBudgetMilliseconds);
        s_GpuBudgetMilliseconds = std::max(s_GpuBudgetMilliseconds, 0.0f);
    }
}

void GIProfiler::Initialize(const std::function<GIProfiling::Report(void)>& describeReport)
{
    s_DescribeReport = describeReport;
    UserSettings::Register(kPanelName, SaveSettings, LoadSettings);
}

void GIProfiler::Shutdown(void)
{
    UserSettings::Unregister(kPanelName);
    s_DescribeReport = nullptr;
    s_LastRuns.clear();
}

void GIProfiler::Update(void)
{
    const GIProfiling::Report report = s_DescribeReport();
    const ProfiledScopes profiled = FindProfiledScopes(report.scopes);
    if (profiled.pipeline)
        RecordLastRuns(*profiled.pipeline, std::wstring());
    for (const ScopeTimes& scope : profiled.debug)
        RecordLastRuns(scope, std::wstring());

    ImGuiLayer::ShowPanel(kPanelName, s_ShowPanel, [&] { ShowPanelContent(report, profiled); });
}

void GIProfiler::Measure(uint32_t frames, const ControlChannel::Reply& reply)
{
    if (EngineProfiling::IsPaused())
    {
        reply.Error("the profiler is paused, so its times do not change");
        return;
    }
    const GIProfiling::Scopes scopes = s_DescribeReport().scopes;
    std::vector<MeasuredScope> debug;
    for (const wchar_t* scope : scopes.debug)
        debug.push_back({ .name = scope });
    MeasureNextFrame(std::make_shared<Measurement>(Measurement{
        .frames = frames,
        .measured = 0,
        .scopes = scopes,
        .pipeline = { .name = scopes.pipeline },
        .debug = std::move(debug),
        .reply = reply,
    }));
}
