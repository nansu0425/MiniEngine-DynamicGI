#pragma once

// ray tracing의 기능인 AS, closest hit, visibility query를 보고 검증하는 도구를 종류별 section으로 담는 panel이다.
// section의 제목과 순서만 정하고, 내용은 각 도구가 그린다.
// PT, DDGI처럼 ray tracing을 쓰는 rendering 기법은 ray tracing의 기능이 아니므로 그 설정은 여기에 두지 않는다.
namespace RaytracingPanel
{
    // ImGuiLayer::BeginFrame과 Render 사이에서 부른다.
    void Update(void);
}
