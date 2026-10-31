#pragma once

#include "Core/Automation/ControlChannel.h"

#include <functional>
#include <map>
#include <string>

// harness 명령이 key와 value의 묶음 하나를 "<name>: <key>=<value>; <key>=<value>"로 바꾼다.
// 묶음은 형식이 틀린 값을 조용히 버리거나 다른 값으로 읽을 수 있으므로, 적용한 뒤 다시 읽은 값이 요청과 같을 때만 바꾼 것으로 둔다.
namespace SettingsAssignment
{
    typedef std::map<std::string, std::string> Values;

    // ':' 앞을 name에, 뒤를 assignments에 담는다. ':'가 없으면 false를 돌려준다.
    bool Split(const std::string& arguments, std::string& name, std::string& assignments);

    // read는 묶음의 key를 모두 채우고, apply는 그렇게 채운 values를 적용한다. assignments의 key는 read가 채운 key여야 한다.
    // 하나라도 받지 않으면 처음 값으로 되돌리고 reply로 error를 보낸 뒤 false를 돌려준다. 모두 받으면 reply를 보내지 않고,
    // applied에 요청한 key마다 다시 읽은 값을 담아 true를 돌려준다. name은 error가 묶음을 가리키는 이름이다.
    bool Assign(const std::string& name, const std::string& assignments, const std::function<void(Values&)>& read,
        const std::function<void(const Values&)>& apply, ControlChannel::Fields& applied, const ControlChannel::Reply& reply);

    ControlChannel::Fields Describe(const Values& values);
}
