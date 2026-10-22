#include "Model/Raytracing/RaytracingView.h"
#include "Model/Raytracing/RaytracingDebugView.h"
#include "Model/Raytracing/RayQueryValidation.h"
#include "Core/pch.h"

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

    Selection s_Selection = { kOff, 0 };
    bool s_ShowSingleSidedBackFaces = false;
    RayQueryValidation::Heatmap s_Heatmap;

    RayQueryValidation::Mode GetValidationMode(void)
    {
        return (RayQueryValidation::Mode)s_Selection.view;
    }

    std::string GetSelectionLabel(void)
    {
        if (s_Selection.group == kDebug)
            return std::string("Debug: ") + RaytracingDebugView::GetViewLabel((RaytracingDebugView::View)s_Selection.view);
        if (s_Selection.group == kValidation)
            return std::string("Validation: ") + RayQueryValidation::GetModeLabel(GetValidationMode());
        return "Off";
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
        if (!ImGui::BeginCombo("##View", GetSelectionLabel().c_str()))
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

        RayQueryValidation::Heatmap fitted = s_Heatmap;
        ImGui::BeginDisabled(!metric || !RayQueryValidation::FitHeatmapToCapture(mode, fitted));
        if (ImGui::Button("Fit to Capture"))
            s_Heatmap = fitted;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Fits the range to p1 ~ p99 of this metric in the last capture of this view. The top 1%% stays darkest.");
        ImGui::EndDisabled();

        ImGui::EndDisabled();
    }
}

void RaytracingView::Initialize(void)
{
    RaytracingDebugView::Initialize();
    RayQueryValidation::Initialize();
}

void RaytracingView::Shutdown(void)
{
    RayQueryValidation::Shutdown();
}

void RaytracingView::ShowSection(void)
{
    ShowViewCombo();
    ShowBackFacesCheckbox();
    ShowHeatmapOptions();
}

void RaytracingView::Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, const Math::Vector3& sunDirection, ColorBuffer& target)
{
    if (s_Selection.group == kDebug)
    {
        RaytracingDebugView::Render(context, (RaytracingDebugView::View)s_Selection.view, !s_ShowSingleSidedBackFaces,
            camera, rasterViewport, scene, sunDirection, target);
    }
    else if (s_Selection.group == kValidation)
    {
        RayQueryValidation::Render(context, GetValidationMode(), s_Heatmap, camera, rasterViewport, scene, target);
    }
}
