#include "Core/pch.h"
#include "Core/Editor/MeasurementConditions.h"

#include <imgui.h>
#include <cerrno>

namespace
{
    std::map<std::string, MeasurementConditions::Group> s_Groups;
    std::string s_UnavailableReason;

    std::string JoinKeys(const std::vector<std::string>& keys)
    {
        std::string joined;
        for (const std::string& key : keys)
            joined += (joined.empty() ? "" : ", ") + key;
        return joined;
    }
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

void MeasurementConditions::SetUnavailableReason(const std::string& reason)
{
    s_UnavailableReason = reason;
}

const std::string& MeasurementConditions::GetUnavailableReason(void)
{
    return s_UnavailableReason;
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
        paths.insert(paths.end(), group.second.statePaths.begin(), group.second.statePaths.end());
    return paths;
}

void MeasurementConditions::RestoreField::SetLine(const std::string& line)
{
    strncpy_s(m_Line, line.c_str(), _TRUNCATE);
    m_HasResult = false;
}

void MeasurementConditions::RestoreField::Show(void)
{
    ImGui::PushID(this);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##Line", m_Line, sizeof(m_Line)))
        m_HasResult = false;
    ImGui::SetItemTooltip("A line printed by Log, left beside a capture or given by describe.");

    const std::string& unavailable = GetUnavailableReason();
    ImGui::BeginDisabled(!unavailable.empty() || m_Line[0] == '\0');
    if (ImGui::Button("Restore"))
    {
        m_Result = Restore(Parse(m_Line));
        m_HasResult = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", unavailable.empty() ?
        "Sets every condition in the line, or nothing if the line lacks one or one cannot be set. The resolution is only compared." :
        unavailable.c_str());

    const ImVec4 kWarningColor(1.0f, 0.75f, 0.2f, 1.0f);
    if (m_HasResult && !m_Result.unread.empty())
        ImGui::TextColored(kWarningColor, "Warning: nothing was restored. The line lacks or cannot set %s.", JoinKeys(m_Result.unread).c_str());
    if (m_HasResult)
    {
        for (const std::string& note : m_Result.notes)
            ImGui::TextColored(kWarningColor, "Warning: %s", note.c_str());
    }
    ImGui::PopID();
}
