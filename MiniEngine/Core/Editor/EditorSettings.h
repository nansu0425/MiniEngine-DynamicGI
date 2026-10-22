#pragma once

#include <functional>
#include <map>
#include <string>

// 종료할 때 editor의 상태를 working directory의 editor.ini에 쓰고, 다음 실행 때 되돌린다.
// 무엇을 저장할지는 각 기능이 section으로 등록해 정한다. panel의 배치는 ImGui가 imgui.ini에 따로 저장한다.
namespace EditorSettings
{
    // section 안의 key와 value
    typedef std::map<std::string, std::string> Values;

    // save는 Save 때 section의 값을 채우고, load는 editor.ini에 그 section이 있을 때 그 값을 받는다.
    // Load 뒤에 등록한 section은 등록할 때 바로 load를 받는다.
    void Register(const std::string& section, const std::function<void(Values&)>& save, const std::function<void(const Values&)>& load);
    void Unregister(const std::string& section);

    // app이 기본값을 정한 뒤에 한 번 부른다.
    void Load(void);
    // 등록되지 않은 section은 읽은 값을 그대로 다시 쓴다. 그래서 이번 실행에서 쓰지 않은 기능의 상태도 남는다.
    void Save(void);
}
