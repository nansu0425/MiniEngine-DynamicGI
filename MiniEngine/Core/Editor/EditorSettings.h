#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

// 종료할 때 editor의 상태를 editor.ini에 쓰고, 다음 실행 때 되돌린다.
// 무엇을 저장할지는 각 기능이 section으로 등록해 정한다. panel의 배치는 ImGui가 imgui.ini에 따로 저장한다.
namespace EditorSettings
{
    // editor.ini와 imgui.ini의 경로. -settings_dir <dir>이 있으면 working directory 대신 그 폴더에 둔다.
    // harness가 사용자의 파일을 건드리지 않고 정해진 상태에서 시작하게 하려는 것이다.
    std::wstring GetFilePath(const std::wstring& fileName);

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

    // 실행 중에 section의 상태를 읽고 바꾼다. 등록되지 않은 section이면 false를 돌려준다.
    // load는 section의 key가 모두 있어야 적용하는 경우가 있으므로, 바꿀 때는 ReadSection의 결과에 바꿀 값을 덮어써서 넘긴다.
    std::vector<std::string> GetSectionNames(void);
    bool ReadSection(const std::string& section, Values& values);
    bool ApplySection(const std::string& section, const Values& values);
}
