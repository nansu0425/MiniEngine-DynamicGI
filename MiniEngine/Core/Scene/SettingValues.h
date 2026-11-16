#pragma once

#include "Core/Math/Vector.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>

namespace SettingValues
{
    typedef std::map<std::string, std::string> Values;

    // count개의 숫자를 ','로 이은 text. 끝에 다른 글자가 있거나 유한하지 않은 숫자는 받지 않는다.
    bool ReadNumbers(const std::string& text, float* numbers, int count);
    bool ReadVector(const std::string& text, Math::Vector3& vector);
    // 다시 읽으면 같은 float가 되는 자릿수로 쓴다.
    std::string FormatVector(Math::Vector3 vector);

    std::string FormatFlag(bool value);

    void ReadUInt(const Values& values, const char* key, uint32_t& value);
    void ReadFloat(const Values& values, const char* key, float& value);
    void ReadFlag(const Values& values, const char* key, bool& value);

    template <typename Enum, size_t NumNames>
    void ReadName(const Values& values, const char* key, const char* const (&names)[NumNames], Enum& value)
    {
        const auto text = values.find(key);
        if (text == values.end())
            return;
        const auto name = std::find(std::begin(names), std::end(names), text->second);
        if (name != std::end(names))
            value = (Enum)(name - std::begin(names));
    }
}
