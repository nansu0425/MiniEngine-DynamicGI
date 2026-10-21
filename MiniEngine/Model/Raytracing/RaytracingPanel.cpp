#include "Model/Raytracing/RaytracingPanel.h"
#include "Model/Raytracing/RaytracingView.h"
#include "Core/pch.h"
#include "Core/UI/ImGuiLayer.h"

#include <imgui.h>

namespace
{
    bool s_ShowPanel = false;

    // section마다 ID 범위를 따로 두어, section 안의 widget이 section 제목이나 다른 section의 widget과 label이 같아도 부딪히지 않게 한다.
    void ShowSection(const char* title, void (*showContent)(void))
    {
        if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::PushID(title);
        showContent();
        ImGui::PopID();
    }
}

void RaytracingPanel::Update(void)
{
    ImGuiLayer::ShowPanel("Ray Tracing", s_ShowPanel, []
    {
        ShowSection("View", RaytracingView::ShowSection);
    });
}
