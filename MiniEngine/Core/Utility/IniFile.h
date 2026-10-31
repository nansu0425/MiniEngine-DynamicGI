#pragma once

#include <map>
#include <string>

// "[section]" 줄 아래에 "key=value" 줄을 두는 text 파일. ';'로 시작하는 줄은 주석이고, '='가 없는 줄은 읽지 않는다.
// user.ini와 ModelViewer의 scene file이 이 형식이다.
namespace IniFile
{
    typedef std::map<std::string, std::string> Values;
    typedef std::map<std::string, Values> Sections;

    // 파일을 열 수 없으면 비어 있다. 같은 section이 여러 번 나오면 key를 합친다.
    Sections Read(const std::wstring& path);

    // path의 파일에서 sections에 있는 section의 key 줄만 그 값으로 바꾸고, 주석과 다른 section은 그대로 둔다. 파일에 없던 key는 그
    // section의 마지막 key 뒤에, 없던 section은 파일 끝에 둔다. sections의 section에 없는 key의 줄은 지운다. 줄 끝은 파일의 것을 따른다.
    // 파일을 읽거나 쓸 수 없으면 false를 돌려준다.
    bool WriteSections(const std::wstring& path, const Sections& sections);
}
