#include "Core/pch.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/App/GameInput.h"
#include "Core/UI/ImGuiLayer.h"

#include <algorithm>

using namespace Math;

namespace
{
    enum class GizmoMode { Translate, Rotate };

    std::vector<SceneEditor::Object*> s_Objects;
    SceneEditor::Object* s_Selected = nullptr;
    GizmoMode s_GizmoMode = GizmoMode::Translate;

    // Unreal, Unity와 같은 key를 쓴다. camera를 조작하는 동안에는 이 key가 이동 key다.
    void UpdateGizmoMode(void)
    {
        if (GameInput::IsMouseCapturedByCamera())
            return;

        if (GameInput::IsFirstPressed(GameInput::kKey_w))
            s_GizmoMode = GizmoMode::Translate;
        else if (GameInput::IsFirstPressed(GameInput::kKey_e))
            s_GizmoMode = GizmoMode::Rotate;
    }

    void EditSelected(const Camera& camera)
    {
        if (s_GizmoMode == GizmoMode::Rotate)
        {
            s_Selected->Rotate(camera);
            return;
        }

        Matrix4 transform(AffineTransform(s_Selected->GetPosition()));
        if (SceneEditor::Manipulate(camera, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, transform))
            s_Selected->SetPosition(Vector3(transform.GetW()));
    }

    // icon이 cursor 아래에 있는 물체 중 icon 중심이 cursor에 가장 가까운 것
    SceneEditor::Object* FindObjectUnderCursor(const Camera& camera)
    {
        const ImVec2 cursor = ImGui::GetMousePos();
        const float radius = SceneEditor::GetIconRadius();
        SceneEditor::Object* nearest = nullptr;
        float nearestDistanceSq = radius * radius;
        for (SceneEditor::Object* object : s_Objects)
        {
            ImVec2 icon;
            if (!SceneEditor::ProjectToScene(camera, object->GetPosition(), icon))
                continue;

            const float dx = icon.x - cursor.x;
            const float dy = icon.y - cursor.y;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq <= nearestDistanceSq)
            {
                nearest = object;
                nearestDistanceSq = distanceSq;
            }
        }
        return nearest;
    }
}

void SceneEditor::AddObject(Object& object)
{
    s_Objects.push_back(&object);
}

void SceneEditor::RemoveObject(Object& object)
{
    s_Objects.erase(std::remove(s_Objects.begin(), s_Objects.end(), &object), s_Objects.end());
    if (s_Selected == &object)
        s_Selected = nullptr;
}

void SceneEditor::Update(const Camera& camera)
{
    if (!ImGuiLayer::BeginSceneGizmo())
        return;

    UpdateGizmoMode();

    for (Object* object : s_Objects)
        object->Draw(camera, object == s_Selected);

    if (s_Selected != nullptr)
        EditSelected(camera);

    // gizmo나 다른 창 위의 click은 ImGui가 mouse를 가져가므로 물체를 고르는 데 쓰지 않는다.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse &&
        !ImGuizmo::IsUsingAny() && !GameInput::IsMouseCapturedByCamera())
    {
        s_Selected = FindObjectUnderCursor(camera);
    }

    ImGuiLayer::EndSceneGizmo();
}

float SceneEditor::GetIconRadius(void)
{
    return ImGui::GetFontSize() * 0.75f;
}

bool SceneEditor::ProjectToScene(const Camera& camera, Vector3 position, ImVec2& screenPosition)
{
    const Vector4 clip = camera.GetViewProjMatrix() * position;
    const float w = clip.GetW();
    if (w < camera.GetNearClip())
        return false;

    const float x = clip.GetX() / clip.GetW();
    const float y = clip.GetY() / clip.GetW();
    const RECT scene = ImGuiLayer::GetSceneViewport();
    screenPosition.x = scene.left + (0.5f + 0.5f * x) * (scene.right - scene.left);
    screenPosition.y = scene.top + (0.5f - 0.5f * y) * (scene.bottom - scene.top);
    return true;
}

float SceneEditor::GetWorldLengthPerPixel(const Camera& camera, Vector3 position)
{
    const float depth = Dot(position - camera.GetPosition(), camera.GetForwardVec());
    const RECT scene = ImGuiLayer::GetSceneViewport();
    return 2.0f * depth * tanf(camera.GetFOV() * 0.5f) / (float)(scene.bottom - scene.top);
}

bool SceneEditor::Manipulate(const Camera& camera, ImGuizmo::OPERATION operation, ImGuizmo::MODE mode, Matrix4& transform)
{
    // Matrix4의 memory 배치는 ImGuizmo가 받는 matrix의 배치와 같다.
    return ImGuizmo::Manipulate((const float*)&camera.GetViewMatrix(), (const float*)&camera.GetProjMatrix(),
        operation, mode, (float*)&transform);
}
