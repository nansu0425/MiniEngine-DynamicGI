#pragma once

#include "Core/Camera/Camera.h"
#include "Core/Scene/Scene.h"

#include <functional>
#include <typeinfo>

#include <imgui.h>
#include <ImGuizmo.h>

// game engine의 editor처럼 Scene에 등록한 object를 Scene Hierarchy panel에 나열하고, 고른 object의 component를 Details panel에 표시한다.
// icon이 있는 object는 scene 위에 icon으로도 보여 주고, icon을 클릭해 고른 object를 gizmo로 바꾼다.
namespace SceneEditor
{
    // 스스로는 보이지 않는 object를 scene 위에 icon으로 표시한다. 고르면 gizmo로 옮기고 돌린다. 이 component가 없는 object는
    // Scene Hierarchy로만 고르고 gizmo로 바꾸지 않는다.
    class IconComponent : public Scene::Component
    {
    public:
        // icon을 그리고 클릭으로 고르는 자리
        virtual Math::Vector3 GetPosition() const = 0;
        virtual void SetPosition(Math::Vector3 position) = 0;

        // GetPosition에 GetIconRadius 크기의 icon을 그리고, 그 밖에 object를 나타내는 것을 scene 위에 그린다.
        virtual void Draw(const Math::Camera& camera, bool selected) const = 0;

        // rotate mode에서 고른 object를 Manipulate로 돌린다. axes는 gizmo의 ring이 object의 축(LOCAL)과 world의 축(WORLD) 중
        // 어느 것을 따르는지다. 돌릴 수 없는 object는 아무것도 하지 않는다.
        virtual void Rotate(const Math::Camera& camera, ImGuizmo::MODE axes) = 0;
    };

    // Details panel은 고른 object의 이름 아래에, show를 등록한 type의 component를 component 순서대로 표시한다.
    // show는 T와 type이 같은 component만 그린다. T를 상속한 type의 component는 그리지 않는다.
    // Details panel에서 바꾸는 scene 설정은 component의 setting key로 둔다. harness의 editor get, set 명령은 Details panel을 거치지 않고
    // setting key로 object의 설정을 읽고 바꾸기 때문이다.
    void RegisterComponentDetails(const std::type_info& componentType, std::function<void(Scene::Component&)> show);
    template<class T> void RegisterComponentDetails(std::function<void(T&)> show)
    {
        RegisterComponentDetails(typeid(T), [show](Scene::Component& component) { show(static_cast<T&>(component)); });
    }

    // camera를 이번 frame에 맞춘 뒤 ImGuiLayer::BeginFrame과 Render 사이에서 부른다. View 메뉴의 Scene Hierarchy, Details panel과
    // scene 위의 gizmo toolbar도 그린다.
    void Update(const Math::Camera& camera);

    // harness가 icon click, Scene Hierarchy panel, W, E key와 gizmo toolbar 대신 명령으로 고른 object, gizmo mode, rotate의 축을 정하고,
    // state의 editor로 읽는다. Details panel 대신 명령으로 object의 설정을 읽고 바꾼다. camera는 Update에 넘기는 것과 같은 camera이고 app이 끝날 때까지 살아 있어야 한다. icon의 화면 위치를 이것으로 투영한다.
    void RegisterCommands(const Math::Camera& camera);

    bool IsSceneClicked(void);

    // 아래 함수는 IconComponent의 Draw와 Rotate 안에서, 그리고 scene 위에 보이는 것을 click으로 고를 때 쓴다.
    float GetIconRadius(void);
    // camera 뒤에 있으면 false를 반환한다.
    bool ProjectToScene(const Math::Camera& camera, Math::Vector3 position, ImVec2& screenPosition);
    // 화면에서 크기가 일정한 것을 그릴 때 쓰는, position에서 scene 1 pixel에 해당하는 world 길이
    float GetWorldLengthPerPixel(const Math::Camera& camera, Math::Vector3 position);
    bool Manipulate(const Math::Camera& camera, ImGuizmo::OPERATION operation, ImGuizmo::MODE mode, Math::Matrix4& transform);
}
