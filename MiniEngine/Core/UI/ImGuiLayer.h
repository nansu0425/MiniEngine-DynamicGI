#pragma once

#include <functional>

namespace ImGuiLayer
{
    void Initialize(void);
    void Shutdown(void);

    // BeginFrame과 Render 사이에서는 어디서든 ImGui widget을 호출할 수 있다.
    void BeginFrame(void);
    // BeginFrame이 정한 이번 frame의 scene 영역(client 좌표, pixel). UI를 숨기면 window 전체다.
    RECT GetSceneViewport(void);
    // 이번 frame의 widget을 g_WindowOverlayBuffer에 그린다.
    void Render(void);

    // 사이에서 그린 widget은 scene 영역 안에만 보이고, UI를 숨겨도 보인다.
    // ImGui::Begin과 달리 false를 반환하면 End를 부르지 않는다.
    bool BeginSceneOverlay(void);
    void EndSceneOverlay(void);

    // View 메뉴의 name 항목으로 여닫는 창이다. UI를 숨기면 메뉴 항목과 창 모두 그리지 않는다.
    void ShowPanel(const char* name, bool& open, const std::function<void()>& drawContent);

    // true를 반환하면 window procedure는 그 message를 더 처리하지 않는다.
    bool HandleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
}
