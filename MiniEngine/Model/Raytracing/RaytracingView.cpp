#include "Model/Raytracing/RaytracingView.h"
#include "Model/Raytracing/RaytracingDebugView.h"
#include "Model/Raytracing/RayQueryValidation.h"
#include "Core/pch.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/EditorSettings.h"

#include <imgui.h>

namespace
{
    enum Group { kOff, kDebug, kValidation };

    struct Selection
    {
        Group group;
        // group 안에서의 view 순서
        int view;

        bool operator==(const Selection& other) const { return group == other.group && view == other.view; }
    };

    const char* kSettingsSection = "Ray Tracing View";

    Selection s_Selection = { kOff, 0 };
    bool s_ShowSingleSidedBackFaces = false;
    RayQueryValidation::Heatmap s_Heatmap;

    RayQueryValidation::Mode GetValidationMode(void)
    {
        return (RayQueryValidation::Mode)s_Selection.view;
    }

    std::string GetSelectionLabel(Selection selection)
    {
        if (selection.group == kDebug)
            return std::string("Debug: ") + RaytracingDebugView::GetViewLabel((RaytracingDebugView::View)selection.view);
        if (selection.group == kValidation)
            return std::string("Validation: ") + RayQueryValidation::GetModeLabel((RayQueryValidation::Mode)selection.view);
        return "Off";
    }

    // view의 순서가 바뀌어도 같은 view를 고르도록 label로 찾는다.
    bool FindSelection(const std::string& label, Selection& selection)
    {
        std::vector<Selection> candidates = { { kOff, 0 } };
        for (int view = 0; view < RaytracingDebugView::kNumViews; ++view)
            candidates.push_back({ kDebug, view });
        for (int mode = 0; mode < RayQueryValidation::kNumModes; ++mode)
            candidates.push_back({ kValidation, mode });

        for (const Selection& candidate : candidates)
        {
            if (GetSelectionLabel(candidate) == label)
            {
                selection = candidate;
                return true;
            }
        }
        return false;
    }

    void Select(Selection selection)
    {
        s_Selection = selection;
        if (selection.group == kValidation)
            s_Heatmap = RayQueryValidation::GetDefaultHeatmap(GetValidationMode(), 0);
    }

    void SelectableView(const char* label, Selection selection)
    {
        const bool selected = s_Selection == selection;
        if (ImGui::Selectable(label, selected) && !selected)
            Select(selection);
        if (selected)
            ImGui::SetItemDefaultFocus();
    }

    // 그룹마다 제목 아래에 view를 나열하고, 전체에서 하나만 고르게 한다.
    void ShowViewCombo(void)
    {
        // section 제목이 이미 View이므로 label 없이 줄 전체를 쓴다.
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (!ImGui::BeginCombo("##View", GetSelectionLabel(s_Selection).c_str()))
            return;

        SelectableView("Off", { kOff, 0 });

        ImGui::SeparatorText("Debug");
        ImGui::PushID("Debug");
        for (int view = 0; view < RaytracingDebugView::kNumViews; ++view)
            SelectableView(RaytracingDebugView::GetViewLabel((RaytracingDebugView::View)view), { kDebug, view });
        ImGui::PopID();

        ImGui::SeparatorText("Validation");
        ImGui::PushID("Validation");
        for (int mode = 0; mode < RayQueryValidation::kNumModes; ++mode)
            SelectableView(RayQueryValidation::GetModeLabel((RayQueryValidation::Mode)mode), { kValidation, mode });
        ImGui::PopID();

        ImGui::EndCombo();
    }

    // 모든 ray가 따르는 culling 규칙은 그대로 두고, debug view의 ray만 그 규칙이 cull하는 뒷면에 맞게 한다.
    void ShowBackFacesCheckbox(void)
    {
        ImGui::BeginDisabled(s_Selection.group != kDebug);
        ImGui::Checkbox("Show Single-Sided Back Faces", &s_ShowSingleSidedBackFaces);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("All rays cull the back faces of single-sided materials. When on, only debug view rays hit them.");
    }

    // Fit to Capture가 맞출 수 있으면 맞춘 heatmap을, 맞출 수 없으면 그 이유를 준다. 버튼이 disabled인 조건과 validation fit이 거절하는 조건이 같다.
    bool FitHeatmap(RayQueryValidation::Heatmap& fitted, std::string& reason)
    {
        if (s_Selection.group != kValidation)
        {
            reason = "the view is not a Validation view. Pick one as View in the Ray Tracing View settings.";
            return false;
        }
        const RayQueryValidation::Mode mode = GetValidationMode();
        if (!RayQueryValidation::IsMetricItem(mode, s_Heatmap.item))
        {
            reason = std::string("the heatmap is the category ") + RayQueryValidation::GetHeatmapItemLabel(mode, s_Heatmap.item) +
                ", not a metric. Pick a metric as Heatmap in the Ray Tracing View settings.";
            return false;
        }
        fitted = s_Heatmap;
        if (!RayQueryValidation::FitHeatmapToCapture(mode, fitted))
        {
            reason = "the last capture is not of this view or has no sample of this metric. validation capture first.";
            return false;
        }
        return true;
    }

    void FitHeatmapByCommand(const ControlChannel::Reply& reply)
    {
        RayQueryValidation::Heatmap fitted;
        std::string reason;
        if (!FitHeatmap(fitted, reason))
        {
            reply.Error(reason);
            return;
        }
        s_Heatmap = fitted;
        reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState(std::string("settings.") + kSettingsSection)));
    }

    // Validation view의 결과 중 overlay에 칠할 category나 metric과, metric을 칠할 범위를 고른다.
    void ShowHeatmapOptions(void)
    {
        const bool validation = s_Selection.group == kValidation;
        const RayQueryValidation::Mode mode = GetValidationMode();

        ImGui::BeginDisabled(!validation);
        if (ImGui::BeginCombo("Heatmap", validation ? RayQueryValidation::GetHeatmapItemLabel(mode, s_Heatmap.item) : "-"))
        {
            for (int item = 0; item < RayQueryValidation::GetNumHeatmapItems(mode); ++item)
            {
                const bool selected = s_Heatmap.item == item;
                if (ImGui::Selectable(RayQueryValidation::GetHeatmapItemLabel(mode, item), selected) && !selected)
                    s_Heatmap = RayQueryValidation::GetDefaultHeatmap(mode, item);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Validation views only. Pixels in a category are orange over the scene. Metric values are opaque blue, darker for larger values.");

        const bool metric = validation && RayQueryValidation::IsMetricItem(mode, s_Heatmap.item);
        ImGui::BeginDisabled(!metric);
        ImGui::DragIntRange2("Range (log2)", &s_Heatmap.minExponent, &s_Heatmap.maxExponent, 0.1f,
            RayQueryValidation::kMinHeatmapExponent, RayQueryValidation::kMaxHeatmapExponent, "2^%d", "2^%d");
        ImGui::SetItemTooltip("Metric values at the lower end are light blue and at the upper end dark blue.");

        RayQueryValidation::Heatmap fitted;
        std::string reason;
        ImGui::BeginDisabled(!FitHeatmap(fitted, reason));
        if (ImGui::Button("Fit to Capture"))
            s_Heatmap = fitted;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Fits the range to p1 ~ p99 of this metric in the last capture of this view. The top 1%% stays darkest.");
        ImGui::EndDisabled();

        ImGui::EndDisabled();
    }

    void SaveSettings(EditorSettings::Values& values)
    {
        values["View"] = GetSelectionLabel(s_Selection);
        values["Show Single-Sided Back Faces"] = s_ShowSingleSidedBackFaces ? "1" : "0";
        if (s_Selection.group != kValidation)
            return;

        char range[32];
        sprintf_s(range, "%d,%d", s_Heatmap.minExponent, s_Heatmap.maxExponent);
        values["Heatmap"] = RayQueryValidation::GetHeatmapItemLabel(GetValidationMode(), s_Heatmap.item);
        values["Heatmap Range"] = range;
    }

    void LoadSettings(const EditorSettings::Values& values)
    {
        const auto backFaces = values.find("Show Single-Sided Back Faces");
        if (backFaces != values.end())
            s_ShowSingleSidedBackFaces = backFaces->second == "1";

        const auto view = values.find("View");
        Selection selection = { kOff, 0 };
        if (view == values.end() || !FindSelection(view->second, selection))
            return;
        Select(selection);
        if (selection.group != kValidation)
            return;

        const RayQueryValidation::Mode mode = GetValidationMode();
        const auto heatmap = values.find("Heatmap");
        for (int item = 0; heatmap != values.end() && item < RayQueryValidation::GetNumHeatmapItems(mode); ++item)
        {
            if (heatmap->second == RayQueryValidation::GetHeatmapItemLabel(mode, item))
                s_Heatmap = RayQueryValidation::GetDefaultHeatmap(mode, item);
        }

        const auto range = values.find("Heatmap Range");
        int minExponent = 0, maxExponent = 0;
        if (range != values.end() && sscanf_s(range->second.c_str(), "%d,%d", &minExponent, &maxExponent) == 2)
        {
            s_Heatmap.minExponent = minExponent;
            s_Heatmap.maxExponent = maxExponent;
        }
    }
}

void RaytracingView::Initialize(void)
{
    RaytracingDebugView::Initialize();
    RayQueryValidation::Initialize();
    RayQueryValidation::SetFitCommand(FitHeatmapByCommand);
    EditorSettings::Register(kSettingsSection, SaveSettings, LoadSettings);
}

void RaytracingView::Shutdown(void)
{
    EditorSettings::Unregister(kSettingsSection);
    RayQueryValidation::Shutdown();
}

void RaytracingView::ShowSection(void)
{
    ShowViewCombo();
    ShowBackFacesCheckbox();
    ShowHeatmapOptions();
}

void RaytracingView::Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport, DepthBuffer& rasterDepth,
    const RaytracingScene& scene, const Math::Vector3& lightDirection, ColorBuffer& target)
{
    if (s_Selection.group == kDebug)
    {
        RaytracingDebugView::Render(context, (RaytracingDebugView::View)s_Selection.view, !s_ShowSingleSidedBackFaces,
            camera, rasterViewport, scene, lightDirection, target);
    }
    else if (s_Selection.group == kValidation)
    {
        RayQueryValidation::Render(context, GetValidationMode(), s_Heatmap, camera, rasterViewport, rasterDepth, scene, target);
    }
}
