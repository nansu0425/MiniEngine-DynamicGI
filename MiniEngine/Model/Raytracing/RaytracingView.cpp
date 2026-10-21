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

    std::string GetSelectionLabel(void)
    {
        if (s_Selection.group == kDebug)
            return std::string("Debug: ") + RaytracingDebugView::GetViewLabel((RaytracingDebugView::View)s_Selection.view);
        return "Off";
    }

    void SelectableView(const char* label, Selection selection)
    {
        const bool selected = s_Selection == selection;
        if (ImGui::Selectable(label, selected))
            s_Selection = selection;
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
}

void RaytracingView::Initialize(void)
{
    RaytracingDebugView::Initialize();
}

void RaytracingView::ShowSection(void)
{
    ShowViewCombo();
    ShowBackFacesCheckbox();
}

void RaytracingView::Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, const Math::Vector3& sunDirection, ColorBuffer& target)
{
    if (s_Selection.group == kDebug)
    {
        RaytracingDebugView::Render(context, (RaytracingDebugView::View)s_Selection.view, !s_ShowSingleSidedBackFaces,
            camera, rasterViewport, scene, sunDirection, target);
    }
}
