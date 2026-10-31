#include "Core/pch.h"
#include "Core/App/UserSettings.h"
#include "Core/Utility/IniFile.h"

#include <fstream>

namespace
{
    const wchar_t* kFileName = L"user.ini";

    struct Section
    {
        std::function<void(UserSettings::Values&)> save;
        std::function<void(const UserSettings::Values&)> load;
    };

    std::map<std::string, Section> s_Sections;
    // user.ini에서 읽은 값. 등록되지 않은 section도 Save가 다시 쓸 수 있게 남겨 둔다.
    std::map<std::string, UserSettings::Values> s_LoadedValues;
    std::map<std::string, UserSettings::Values> s_DefaultValues;
    bool s_Loaded = false;

    void RecordDefaults(const std::string& name, const Section& section)
    {
        UserSettings::Values& defaults = s_DefaultValues[name];
        defaults.clear();
        section.save(defaults);
    }

    void ApplyLoadedValues(const std::string& name, const Section& section)
    {
        const auto loaded = s_LoadedValues.find(name);
        if (loaded != s_LoadedValues.end())
            section.load(loaded->second);
    }
}

std::wstring UserSettings::GetFilePath(const std::wstring& fileName)
{
    std::wstring directory;
    if (!CommandLineArgs::GetString(L"settings_dir", directory) || directory.empty())
        return fileName;
    if (directory.back() != L'\\' && directory.back() != L'/')
        directory += L'\\';
    return directory + fileName;
}

void UserSettings::Register(const std::string& section, const std::function<void(Values&)>& save, const std::function<void(const Values&)>& load)
{
    s_Sections[section] = { save, load };
    if (s_Loaded)
    {
        RecordDefaults(section, s_Sections[section]);
        ApplyLoadedValues(section, s_Sections[section]);
    }
}

void UserSettings::Unregister(const std::string& section)
{
    s_Sections.erase(section);
    s_DefaultValues.erase(section);
}

void UserSettings::Load(void)
{
    s_LoadedValues = IniFile::Read(GetFilePath(kFileName));

    s_Loaded = true;
    for (const auto& section : s_Sections)
    {
        RecordDefaults(section.first, section.second);
        ApplyLoadedValues(section.first, section.second);
    }
}

void UserSettings::Save(void)
{
    std::map<std::string, Values> sections = s_LoadedValues;
    for (const auto& section : s_Sections)
    {
        Values& values = sections[section.first];
        values.clear();
        section.second.save(values);
    }

    std::ofstream file(GetFilePath(kFileName));
    for (const auto& section : sections)
    {
        file << '[' << section.first << "]\n";
        for (const auto& value : section.second)
            file << value.first << '=' << value.second << '\n';
        file << '\n';
    }
}

std::vector<std::string> UserSettings::GetSectionNames(void)
{
    std::vector<std::string> names;
    for (const auto& section : s_Sections)
        names.push_back(section.first);
    return names;
}

bool UserSettings::ReadSection(const std::string& section, Values& values)
{
    const auto found = s_Sections.find(section);
    if (found == s_Sections.end())
        return false;

    values.clear();
    found->second.save(values);
    return true;
}

bool UserSettings::ApplySection(const std::string& section, const Values& values)
{
    const auto found = s_Sections.find(section);
    if (found == s_Sections.end())
        return false;

    found->second.load(values);
    return true;
}

bool UserSettings::ReadDefaults(const std::string& section, Values& values)
{
    const auto found = s_DefaultValues.find(section);
    if (found == s_DefaultValues.end())
        return false;

    values = found->second;
    return true;
}
