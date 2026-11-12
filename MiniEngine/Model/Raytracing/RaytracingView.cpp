#include "Model/Raytracing/RaytracingView.h"
#include "Model/Raytracing/RaytracingDebugView.h"
#include "Core/pch.h"

#include <imgui.h>

namespace
{
    enum Group { kOff, kDebug };

    struct Selection
    {
        Group group;
        // group 안에서의 view 순서
        int view;

        bool operator==(const Selection& other) const { return group == other.group && view == other.view; }
    };

    Selection s_Selection = { kOff, 0 };
    bool s_ShowSingleSidedBackFaces = false;

    std::string GetSelectionLabel(Selection selection)
    {
        if (selection.group == kDebug)
            return std::string("Debug: ") + RaytracingDebugView::GetViewLabel((RaytracingDebugView::View)selection.view);
        return "Off";
    }

    // view의 순서가 바뀌어도 같은 view를 고르도록 label로 찾는다.
    bool FindSelection(const std::string& label, Selection& selection)
    {
        std::vector<Selection> candidates = { { kOff, 0 } };
        for (int view = 0; view < RaytracingDebugView::kNumViews; ++view)
            candidates.push_back({ kDebug, view });

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

    void SelectableView(const char* label, Selection selection)
    {
        const bool selected = s_Selection == selection;
        if (ImGui::Selectable(label, selected) && !selected)
            s_Selection = selection;
        if (selected)
            ImGui::SetItemDefaultFocus();
    }

    // 그룹마다 제목 아래에 view를 나열하고, 전체에서 하나만 고르게 한다.
    void ShowViewCombo(void)
    {
        if (ImGui::BeginCombo("View", GetSelectionLabel(s_Selection).c_str()))
        {
            SelectableView("Off", { kOff, 0 });

            ImGui::SeparatorText("Debug");
            ImGui::PushID("Debug");
            for (int view = 0; view < RaytracingDebugView::kNumViews; ++view)
                SelectableView(RaytracingDebugView::GetViewLabel((RaytracingDebugView::View)view), { kDebug, view });
            ImGui::PopID();

            ImGui::EndCombo();
        }
        TuningSection::MakeItemResettable("View");
    }

    // 모든 ray가 따르는 culling 규칙은 그대로 두고, debug view의 ray만 그 규칙이 cull하는 뒷면에 맞게 한다.
    void ShowBackFacesCheckbox(void)
    {
        ImGui::BeginDisabled(s_Selection.group != kDebug);
        ImGui::Checkbox("Show Single-Sided Back Faces", &s_ShowSingleSidedBackFaces);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("All rays cull the back faces of single-sided materials. When on, only debug view rays hit them.");
        TuningSection::MakeItemResettable("Show Single-Sided Back Faces");
    }

    void SaveSettings(TuningSection::Values& values)
    {
        values["View"] = GetSelectionLabel(s_Selection);
        values["Show Single-Sided Back Faces"] = s_ShowSingleSidedBackFaces ? "1" : "0";
    }

    void LoadSettings(const TuningSection::Values& values)
    {
        const auto backFaces = values.find("Show Single-Sided Back Faces");
        if (backFaces != values.end())
            s_ShowSingleSidedBackFaces = backFaces->second == "1";

        const auto view = values.find("View");
        Selection selection = { kOff, 0 };
        if (view != values.end() && FindSelection(view->second, selection))
            s_Selection = selection;
    }

    void ShowSettings(void)
    {
        ShowViewCombo();
        ShowBackFacesCheckbox();
    }

    // ray tracing의 기능인 AS, closest hit, visibility query를 보는 view의 설정이다.
    // PT, DDGI처럼 ray tracing을 쓰는 rendering 기법은 ray tracing의 기능이 아니므로 그 설정은 여기에 두지 않는다.
    TuningSection s_Section("Ray Tracing", ShowSettings, SaveSettings, LoadSettings);
}

void RaytracingView::Initialize(void)
{
    RaytracingDebugView::Initialize();
}

void RaytracingView::Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, const Math::Vector3& lightDirection, ColorBuffer& target)
{
    if (s_Selection.group == kDebug)
    {
        RaytracingDebugView::Render(context, (RaytracingDebugView::View)s_Selection.view, !s_ShowSingleSidedBackFaces,
            camera, rasterViewport, scene, lightDirection, target);
    }
}
