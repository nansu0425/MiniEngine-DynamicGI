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
}
