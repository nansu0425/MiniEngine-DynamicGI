#include "Core/pch.h"
#include "Core/Utility/IniFile.h"

#include <fstream>

IniFile::Sections IniFile::Read(const std::wstring& path)
{
    Sections sections;
    std::ifstream file(path);
    std::string line;
    Values* values = nullptr;
    while (std::getline(file, line))
    {
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            values = &sections[line.substr(1, line.size() - 2)];
            continue;
        }

        const size_t separator = line.find('=');
        if (values != nullptr && separator != std::string::npos)
            (*values)[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return sections;
}
