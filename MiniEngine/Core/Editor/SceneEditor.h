#pragma once

#include "Core/Camera/Camera.h"

#include <imgui.h>
#include <ImGuizmo.h>

// game engine의 editor처럼 scene 안의 물체를 icon으로 보여 주고, icon을 클릭해 고른 물체를 gizmo로 바꾼다.
namespace SceneEditor
{
    class Object
    {
    public:
        virtual ~Object() {}

        // harness가 editor 명령과 state에서 이 물체를 가리키는 이름
        virtual const char* GetName() const = 0;

        // icon을 그리고 클릭으로 고르는 자리
        virtual Math::Vector3 GetPosition() const = 0;
        virtual void SetPosition(Math::Vector3 position) = 0;

        // GetPosition에 GetIconRadius 크기의 icon을 그리고, 그 밖에 이 물체를 나타내는 것을 scene 위에 그린다.
        virtual void Draw(const Math::Camera& camera, bool selected) const = 0;

        // rotate mode에서 고른 물체를 Manipulate로 돌린다. 돌릴 수 없는 물체는 아무것도 하지 않는다.
        virtual void Rotate(const Math::Camera& camera) = 0;
    };

    // 등록한 물체는 RemoveObject를 부를 때까지 살아 있어야 한다.
    void AddObject(Object& object);
    void RemoveObject(Object& object);

    // camera를 이번 frame에 맞춘 뒤 ImGuiLayer::BeginFrame과 Render 사이에서 부른다.
    void Update(const Math::Camera& camera);

    // harness가 icon click과 W, E key 대신 명령으로 고른 물체와 gizmo mode를 정하고, state의 editor로 읽는다.
    // camera는 Update에 넘기는 것과 같은 camera이고 app이 끝날 때까지 살아 있어야 한다. icon의 화면 위치를 이것으로 투영한다.
    void RegisterCommands(const Math::Camera& camera);

    // 아래 함수는 Object의 Draw와 Rotate 안에서 쓴다.
    float GetIconRadius(void);
    // camera 뒤에 있으면 false를 반환한다.
    bool ProjectToScene(const Math::Camera& camera, Math::Vector3 position, ImVec2& screenPosition);
    // 화면에서 크기가 일정한 것을 그릴 때 쓰는, position에서 scene 1 pixel에 해당하는 world 길이
    float GetWorldLengthPerPixel(const Math::Camera& camera, Math::Vector3 position);
    bool Manipulate(const Math::Camera& camera, ImGuizmo::OPERATION operation, ImGuizmo::MODE mode, Math::Matrix4& transform);
}
