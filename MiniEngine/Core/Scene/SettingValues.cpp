#include "Core/pch.h"
#include "Core/Scene/SettingValues.h"
#include "Core/Editor/MeasurementConditions.h"

#include <cmath>
#include <sstream>

bool SettingValues::ReadNumbers(const std::string& text, float* numbers, int count)
{
    std::istringstream stream(text);
    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && stream.get() != ',')
            return false;
        if (!(stream >> numbers[i]) || !std::isfinite(numbers[i]))
            return false;
    }
    return stream.peek() == std::char_traits<char>::eof();
}

bool SettingValues::ReadVector(const std::string& text, Math::Vector3& vector)
{
    float numbers[3];
    if (!ReadNumbers(text, numbers, 3))
        return false;
    vector = Math::Vector3(numbers[0], numbers[1], numbers[2]);
    return true;
}

std::string SettingValues::FormatVector(Math::Vector3 vector)
{
    char text[96];
    sprintf_s(text, "%.9g,%.9g,%.9g", (float)vector.GetX(), (float)vector.GetY(), (float)vector.GetZ());
    return text;
}

std::string SettingValues::FormatFlag(bool value)
{
    return value ? "1" : "0";
}

void SettingValues::ReadUInt(const Values& values, const char* key, uint32_t& value)
{
    const auto text = values.find(key);
    uint32_t read = 0;
    if (text != values.end() && MeasurementConditions::ReadUInt(text->second, read))
        value = read;
}

void SettingValues::ReadFloat(const Values& values, const char* key, float& value)
{
    const auto text = values.find(key);
    float read = 0.0f;
    if (text != values.end() && MeasurementConditions::ReadFloat(text->second, read))
        value = read;
}

void SettingValues::ReadFlag(const Values& values, const char* key, bool& value)
{
    const auto text = values.find(key);
    if (text != values.end() && (text->second == "0" || text->second == "1"))
        value = text->second == "1";
}
