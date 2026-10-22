#include "Core/pch.h"
#include "Core/Editor/EditorSettings.h"

#include <fstream>

namespace
{
    const char* kFileName = "editor.ini";

    struct Section
    {
        std::function<void(EditorSettings::Values&)> save;
        std::function<void(const EditorSettings::Values&)> load;
    };

    std::map<std::string, Section> s_Sections;
    // editor.ini에서 읽은 값. 등록되지 않은 section도 Save가 다시 쓸 수 있게 남겨 둔다.
    std::map<std::string, EditorSettings::Values> s_LoadedValues;
    bool s_Loaded = false;

    void ApplyLoadedValues(const std::string& name, const Section& section)
    {
        const auto loaded = s_LoadedValues.find(name);
        if (loaded != s_LoadedValues.end())
            section.load(loaded->second);
    }
}

void EditorSettings::Register(const std::string& section, const std::function<void(Values&)>& save, const std::function<void(const Values&)>& load)
{
    s_Sections[section] = { save, load };
    if (s_Loaded)
        ApplyLoadedValues(section, s_Sections[section]);
}

void EditorSettings::Unregister(const std::string& section)
{
    s_Sections.erase(section);
}

void EditorSettings::Load(void)
{
    std::ifstream file(kFileName);
    std::string line;
    Values* values = nullptr;
    while (std::getline(file, line))
    {
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            values = &s_LoadedValues[line.substr(1, line.size() - 2)];
            continue;
        }

        const size_t separator = line.find('=');
        if (values != nullptr && separator != std::string::npos)
            (*values)[line.substr(0, separator)] = line.substr(separator + 1);
    }

    s_Loaded = true;
    for (const auto& section : s_Sections)
        ApplyLoadedValues(section.first, section.second);
}

void EditorSettings::Save(void)
{
    std::map<std::string, Values> sections = s_LoadedValues;
    for (const auto& section : s_Sections)
    {
        Values& values = sections[section.first];
        values.clear();
        section.second.save(values);
    }

    std::ofstream file(kFileName);
    for (const auto& section : sections)
    {
        file << '[' << section.first << "]\n";
        for (const auto& value : section.second)
            file << value.first << '=' << value.second << '\n';
        file << '\n';
    }
}
