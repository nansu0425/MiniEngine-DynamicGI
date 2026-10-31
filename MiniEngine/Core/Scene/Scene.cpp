#include "Core/pch.h"
#include "Core/Scene/Scene.h"

#include <algorithm>

namespace
{
    std::vector<Scene::Object*> s_Objects;

    bool HasSettingKey(const Scene::Component& component, const std::string& key)
    {
        const std::vector<std::string>& keys = component.GetSettingKeys();
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    }
}

void Scene::Object::Add(std::unique_ptr<Component> component)
{
    for (const std::string& key : component->GetSettingKeys())
    {
        for (const std::unique_ptr<Component>& existing : m_Components)
        {
            ASSERT(!HasSettingKey(*existing, key), "Two components of the object '%s' take the setting key '%s'.",
                m_Name.c_str(), key.c_str());
        }
    }
    m_Components.push_back(std::move(component));
}

void Scene::Object::SaveSettings(IniFile::Values& values) const
{
    for (const std::unique_ptr<Component>& component : m_Components)
    {
        IniFile::Values componentValues;
        component->SaveSettings(componentValues);
        for (const auto& value : componentValues)
        {
            ASSERT(HasSettingKey(*component, value.first), "A component of the object '%s' wrote '%s', which is not its setting key.",
                m_Name.c_str(), value.first.c_str());
            values[value.first] = value.second;
        }
    }
}

void Scene::Object::LoadSettings(const IniFile::Values& values)
{
    for (const std::unique_ptr<Component>& component : m_Components)
    {
        IniFile::Values componentValues;
        for (const std::string& key : component->GetSettingKeys())
        {
            const auto value = values.find(key);
            if (value != values.end())
                componentValues[key] = value->second;
        }
        component->LoadSettings(componentValues);
    }
}

void Scene::AddObject(Object& object)
{
    s_Objects.push_back(&object);
}

void Scene::RemoveObject(Object& object)
{
    s_Objects.erase(std::remove(s_Objects.begin(), s_Objects.end(), &object), s_Objects.end());
}

const std::vector<Scene::Object*>& Scene::GetObjects(void)
{
    return s_Objects;
}

void Scene::LoadObjectSettings(const IniFile::Sections& sections)
{
    for (Object* object : s_Objects)
    {
        const auto section = sections.find(object->GetName());
        object->LoadSettings(section != sections.end() ? section->second : IniFile::Values());
    }
}

IniFile::Sections Scene::SaveObjectSettings(void)
{
    IniFile::Sections sections;
    for (const Object* object : s_Objects)
    {
        IniFile::Values values;
        object->SaveSettings(values);
        if (!values.empty())
            sections[object->GetName()] = values;
    }
    return sections;
}
