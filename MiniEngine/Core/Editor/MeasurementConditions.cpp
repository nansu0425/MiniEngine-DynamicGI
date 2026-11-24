#include "Core/pch.h"
#include "Core/Editor/MeasurementConditions.h"

#include <algorithm>
#include <cerrno>

namespace
{
    std::map<std::string, MeasurementConditions::Group> s_Groups;
}

void MeasurementConditions::AppendValue(std::string& line, const std::string& key, const std::string& value)
{
    if (!line.empty())
        line += "; ";
    line += key + "=" + value;
}

std::string MeasurementConditions::Format(const Values& values)
{
    std::string line;
    for (const auto& value : values)
        AppendValue(line, value.first, value.second);
    return line;
}

MeasurementConditions::Values MeasurementConditions::Parse(const std::string& line)
{
    Values values;
    const size_t prefixEnd = line.rfind("] ", line.find('='));
    size_t start = prefixEnd == std::string::npos ? 0 : prefixEnd + 2;
    while (start < line.size())
    {
        const size_t end = std::min(line.find("; ", start), line.size());
        const std::string token = line.substr(start, end - start);
        const size_t separator = token.find('=');
        if (separator != std::string::npos)
            values[token.substr(0, separator)] = token.substr(separator + 1);
        start = end + 2;
    }
    return values;
}

bool MeasurementConditions::ReadFloat(const std::string& text, float& value)
{
    if (text.empty() || isspace((unsigned char)text[0]))
        return false;
    char* end = nullptr;
    value = strtof(text.c_str(), &end);
    return end == text.c_str() + text.size();
}

std::string MeasurementConditions::FormatFloat(float value)
{
    char text[32];
    sprintf_s(text, "%.9g", value);
    return text;
}

bool MeasurementConditions::ReadUInt(const std::string& text, uint32_t& value)
{
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    errno = 0;
    const unsigned long long number = strtoull(text.c_str(), nullptr, 10);
    if (errno == ERANGE || number > UINT32_MAX)
        return false;
    value = (uint32_t)number;
    return true;
}

bool MeasurementConditions::ReadValue(const Values& values, const std::string& key, Reading& reading, const std::function<bool(const std::string&)>& parse)
{
    const auto text = values.find(key);
    if (text != values.end() && parse(text->second))
        return true;
    reading.unread.push_back(key);
    return false;
}

void MeasurementConditions::Register(const std::string& name, const Group& group)
{
    s_Groups[name] = group;
}

void MeasurementConditions::Unregister(const std::string& name)
{
    s_Groups.erase(name);
}

MeasurementConditions::Values MeasurementConditions::Describe(void)
{
    Values values;
    for (const auto& group : s_Groups)
        group.second.describe(values);
    return values;
}

MeasurementConditions::Result MeasurementConditions::Restore(const Values& values)
{
    Reading reading;
    for (const auto& group : s_Groups)
        group.second.read(values, reading);
    for (size_t i = 0; i < reading.preparations.size() && reading.unread.empty(); ++i)
        reading.preparations[i](reading);
    if (reading.unread.empty())
    {
        for (const auto& apply : reading.applies)
            apply();
    }
    return { reading.unread, reading.notes };
}

std::vector<std::string> MeasurementConditions::GetStatePaths(void)
{
    std::vector<std::string> paths;
    for (const auto& group : s_Groups)
    {
        for (const std::string& path : group.second.statePaths)
        {
            if (std::find(paths.begin(), paths.end(), path) == paths.end())
                paths.push_back(path);
        }
    }
    return paths;
}
