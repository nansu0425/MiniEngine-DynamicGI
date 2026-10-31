#pragma once

#include "Core/Utility/IniFile.h"

#include <memory>
#include <string>
#include <vector>

// scene을 이루는 object를 등록받고, object의 설정을 scene file의 section에서 읽고 section에 쓴다. object를 표시하고 바꾸는 editor는
// SceneEditor다.
namespace Scene
{
    // object마다 붙이거나 붙이지 않는 부품. component 하나가 object의 data나 동작 하나를 맡는다.
    class Component
    {
    public:
        // settingKeys는 component가 object의 section에서 읽고 쓰는 key다.
        explicit Component(std::vector<std::string> settingKeys = {}) : m_SettingKeys(std::move(settingKeys)) {}
        virtual ~Component() {}

        const std::vector<std::string>& GetSettingKeys() const { return m_SettingKeys; }

        // object의 설정은 scene 설정이라 scene file에 저장한다. scene에 따라 달라지지 않는 설정은 object의 설정으로 두지 않는다.
        // GetSettingKeys의 key만 쓴다. 쓰지 않은 key의 줄은 scene을 저장할 때 section에서 지워진다.
        virtual void SaveSettings(IniFile::Values&) const {}
        // values에는 GetSettingKeys의 key만 있다. 없거나 받을 수 없는 key는 기본값으로 둔다. 그래서 section이 없는 scene file로 열면
        // 모든 설정이 기본값이다.
        virtual void LoadSettings(const IniFile::Values&) {}

    private:
        std::vector<std::string> m_SettingKeys;
    };

    // scene의 object 하나. 상속하지 않고 component를 붙여 만든다.
    class Object final
    {
    public:
        // name은 Scene Hierarchy panel에 보이고, harness가 editor 명령과 state에서 object를 가리키는 이름이다. scene file에서는
        // 이름이 같은 section이 object의 설정을 갖는다.
        explicit Object(std::string name) : m_Name(std::move(name)) {}
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;

        const std::string& GetName() const { return m_Name; }
        void SetName(std::string name) { m_Name = std::move(name); }

        // args로 만든 T를 component 순서의 끝에 둔다. 이미 있는 component와 같은 setting key를 가지면 ASSERT로 멈춘다.
        template<class T, class... Args> T& AddComponent(Args&&... args)
        {
            std::unique_ptr<T> component = std::make_unique<T>(std::forward<Args>(args)...);
            T& added = *component;
            Add(std::move(component));
            return added;
        }

        const std::vector<std::unique_ptr<Component>>& GetComponents() const { return m_Components; }

        // T이거나 T를 상속한 component 중 처음 것. 없으면 nullptr를 돌려준다.
        template<class T> T* FindComponent() const
        {
            for (const std::unique_ptr<Component>& component : m_Components)
            {
                if (T* found = dynamic_cast<T*>(component.get()))
                    return found;
            }
            return nullptr;
        }

        // component마다 쓴 설정을 values에 모은다. 설정이 없는 object는 아무것도 채우지 않는다.
        void SaveSettings(IniFile::Values& values) const;
        // component마다 values에서 그 component의 setting key만 골라 넘긴다. values에 key가 없는 component도 받는다.
        void LoadSettings(const IniFile::Values& values);

    private:
        void Add(std::unique_ptr<Component> component);

        std::string m_Name;
        std::vector<std::unique_ptr<Component>> m_Components;
    };

    // 등록한 순서가 Scene Hierarchy panel의 순서다. 등록한 object는 RemoveObject를 부를 때까지 살아 있어야 한다.
    void AddObject(Object& object);
    void RemoveObject(Object& object);
    const std::vector<Object*>& GetObjects(void);

    // scene을 열 때 부른다. object마다 sections에서 그 이름의 section을 넘기고, 없으면 빈 section을 넘긴다.
    void LoadObjectSettings(const IniFile::Sections& sections);
    // scene을 저장할 때 부른다. 설정이 있는 object마다 그 이름의 section을 채운다.
    IniFile::Sections SaveObjectSettings(void);
}
