#pragma once

#include "Core/Camera/Camera.h"
#include "Core/Utility/IniFile.h"

#include <imgui.h>
#include <ImGuizmo.h>

// game engine의 editor처럼 scene의 물체를 Scene Hierarchy panel에 나열하고, 고른 물체의 설정을 Details panel에 보인다.
// 물체의 설정은 scene 설정이라 scene file에 저장한다. scene에 따라 달라지지 않는 설정은 물체의 설정으로 두지 않는다.
// icon이 있는 물체는 scene 위에 icon으로도 보여 주고, icon을 클릭해 고른 물체를 gizmo로 바꾼다.
namespace SceneEditor
{
    class IconObject;

    class Object
    {
    public:
        virtual ~Object() {}

        // Scene Hierarchy panel에 보이고, harness가 editor 명령과 state에서 이 물체를 가리키는 이름
        virtual const char* GetName() const = 0;

        // 이 물체를 골랐을 때 Details panel의 내용
        virtual void ShowDetails() = 0;

        // Details panel에서 바꾸는 설정. scene file에서는 이 물체 이름의 section이 갖는다. 설정이 없는 물체는 아무것도 채우지 않는다.
        virtual void SaveSettings(IniFile::Values& values) const = 0;
        // values에 없거나 받을 수 없는 key는 기본값으로 둔다. 그래서 section이 없는 scene file로 열면 모든 설정이 기본값이다.
        virtual void LoadSettings(const IniFile::Values& values) = 0;

        // scene 위에 icon이 없는 물체는 nullptr를 반환한다. 이런 물체는 Scene Hierarchy로만 고르고 gizmo로 바꾸지 않는다.
        virtual IconObject* AsIconObject() { return nullptr; }
    };

    // 스스로는 보이지 않아 scene 위에 icon으로 보이는 물체. 고르면 gizmo로 옮기고 돌린다.
    class IconObject : public Object
    {
    public:
        virtual IconObject* AsIconObject() override final { return this; }

        // icon을 그리고 클릭으로 고르는 자리
        virtual Math::Vector3 GetPosition() const = 0;
        virtual void SetPosition(Math::Vector3 position) = 0;

        // GetPosition에 GetIconRadius 크기의 icon을 그리고, 그 밖에 이 물체를 나타내는 것을 scene 위에 그린다.
        virtual void Draw(const Math::Camera& camera, bool selected) const = 0;

        // rotate mode에서 고른 물체를 Manipulate로 돌린다. axes는 gizmo의 ring이 물체의 축(LOCAL)과 world의 축(WORLD) 중
        // 어느 것을 따르는지다. 돌릴 수 없는 물체는 아무것도 하지 않는다.
        virtual void Rotate(const Math::Camera& camera, ImGuizmo::MODE axes) = 0;
    };

    // 등록한 순서로 Scene Hierarchy panel에 나열한다. 등록한 물체는 RemoveObject를 부를 때까지 살아 있어야 한다.
    void AddObject(Object& object);
    void RemoveObject(Object& object);

    // scene을 열 때 부른다. 물체마다 sections에서 그 이름의 section을 넘기고, 없으면 빈 section을 넘긴다.
    void LoadObjectSettings(const IniFile::Sections& sections);
    // scene을 저장할 때 부른다. 설정이 있는 물체마다 그 이름의 section을 채운다.
    IniFile::Sections SaveObjectSettings(void);

    // camera를 이번 frame에 맞춘 뒤 ImGuiLayer::BeginFrame과 Render 사이에서 부른다. View 메뉴의 Scene Hierarchy, Details panel과
    // scene 위의 gizmo toolbar도 그린다.
    void Update(const Math::Camera& camera);

    // harness가 icon click, Scene Hierarchy panel, W, E key와 gizmo toolbar 대신 명령으로 고른 물체, gizmo mode, rotate의 축을 정하고,
    // state의 editor로 읽는다. Details panel 대신 명령으로 물체의 설정을 읽고 바꾼다. camera는 Update에 넘기는 것과 같은 camera이고 app이 끝날 때까지 살아 있어야 한다. icon의 화면 위치를 이것으로 투영한다.
    void RegisterCommands(const Math::Camera& camera);

    // 아래 함수는 IconObject의 Draw와 Rotate 안에서 쓴다.
    float GetIconRadius(void);
    // camera 뒤에 있으면 false를 반환한다.
    bool ProjectToScene(const Math::Camera& camera, Math::Vector3 position, ImVec2& screenPosition);
    // 화면에서 크기가 일정한 것을 그릴 때 쓰는, position에서 scene 1 pixel에 해당하는 world 길이
    float GetWorldLengthPerPixel(const Math::Camera& camera, Math::Vector3 position);
    bool Manipulate(const Math::Camera& camera, ImGuizmo::OPERATION operation, ImGuizmo::MODE mode, Math::Matrix4& transform);
}
