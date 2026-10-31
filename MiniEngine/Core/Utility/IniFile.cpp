#include "Core/pch.h"
#include "Core/Utility/IniFile.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

namespace
{
    bool ReadSectionName(const std::string& line, std::string& name)
    {
        if (line.size() < 2 || line.front() != '[' || line.back() != ']')
            return false;
        name = line.substr(1, line.size() - 2);
        return true;
    }

    // 주석이 아닌 "key=value" 줄의 '=' 위치. 그런 줄이 아니면 npos다.
    size_t FindSeparator(const std::string& line)
    {
        const size_t separator = line.find('=');
        return separator != std::string::npos && line.front() != ';' ? separator : std::string::npos;
    }
}

IniFile::Sections IniFile::Read(const std::wstring& path)
{
    Sections sections;
    std::ifstream file(path);
    std::string line;
    Values* values = nullptr;
    while (std::getline(file, line))
    {
        std::string name;
        if (ReadSectionName(line, name))
        {
            values = &sections[name];
            continue;
        }

        const size_t separator = FindSeparator(line);
        if (values != nullptr && separator != std::string::npos)
            (*values)[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return sections;
}

bool IniFile::WriteSections(const std::wstring& path, const Sections& sections)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    const std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";

    std::vector<std::string> lines;
    std::set<std::string> writtenSections;
    // 지금 바꾸는 section과 그 section에서 쓴 key. 파일에 없던 key는 missingKeysAt에 넣는다.
    const Values* section = nullptr;
    std::set<std::string> writtenKeys;
    size_t missingKeysAt = 0;
    const auto addMissingKeys = [&]()
    {
        if (section == nullptr)
            return;
        std::vector<std::string> missing;
        for (const auto& value : *section)
        {
            if (writtenKeys.count(value.first) == 0)
                missing.push_back(value.first + '=' + value.second);
        }
        lines.insert(lines.begin() + missingKeysAt, missing.begin(), missing.end());
        section = nullptr;
    };

    for (size_t start = 0; start < text.size();)
    {
        const size_t end = std::min(text.find('\n', start), text.size());
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::string name;
        if (ReadSectionName(line, name))
        {
            addMissingKeys();
            lines.push_back(line);
            const auto found = sections.find(name);
            if (found != sections.end())
            {
                section = &found->second;
                writtenKeys.clear();
                writtenSections.insert(name);
                missingKeysAt = lines.size();
            }
            continue;
        }

        const size_t separator = FindSeparator(line);
        if (section != nullptr && separator != std::string::npos)
        {
            const std::string key = line.substr(0, separator);
            const auto value = section->find(key);
            if (value == section->end())
                continue;
            lines.push_back(key + '=' + value->second);
            writtenKeys.insert(key);
            missingKeysAt = lines.size();
            continue;
        }
        lines.push_back(line);
    }
    addMissingKeys();

    for (const auto& added : sections)
    {
        if (writtenSections.count(added.first) != 0)
            continue;
        if (!lines.empty() && !lines.back().empty())
            lines.push_back(std::string());
        lines.push_back('[' + added.first + ']');
        for (const auto& value : added.second)
            lines.push_back(value.first + '=' + value.second);
    }

    std::ofstream output(path, std::ios::binary);
    for (const std::string& line : lines)
        output << line << newline;
    return output.good();
}
