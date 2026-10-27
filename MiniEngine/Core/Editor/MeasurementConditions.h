#pragma once

#include <map>
#include <string>

// 측정한 값이 어떤 조건에서 나왔는지를 "key=value; key=value" 한 줄로 쓰고 읽는다. Log가 이 줄을 찍고 Restore가 이 줄을 받는다.
namespace MeasurementConditions
{
    // key와 value에는 '='와 "; "를 쓰지 않는다.
    typedef std::map<std::string, std::string> Values;

    // line의 끝에 key=value를 "; "로 이어 쓴다.
    void AppendValue(std::string& line, const std::string& key, const std::string& value);

    // 줄의 key=value들. Log가 앞에 붙인 "[이름] " 같은 말은 첫 key 앞의 "] "까지 건너뛴다.
    Values Parse(const std::string& line);
}
