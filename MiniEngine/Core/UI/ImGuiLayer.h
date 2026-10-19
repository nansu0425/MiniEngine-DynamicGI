#pragma once

namespace ImGuiLayer
{
    void Initialize(void);
    void Shutdown(void);

    // BeginFrame과 Render 사이에서는 어디서든 ImGui widget을 호출할 수 있다.
    void BeginFrame(void);
    // BeginFrame이 정한 이번 frame의 scene 영역(client 좌표, pixel). UI를 숨기면 window 전체다.
    RECT GetSceneViewport(void);
    // 이번 frame의 widget을 g_OverlayBuffer에 그린다.
    void Render(void);

    // true를 반환하면 window procedure는 그 message를 더 처리하지 않는다.
    bool HandleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
}
