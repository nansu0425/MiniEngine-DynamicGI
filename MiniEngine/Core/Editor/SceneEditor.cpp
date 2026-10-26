#include "Core/pch.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/App/GameInput.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/UI/ImGuiLayer.h"

#include <algorithm>

using namespace Math;

namespace
{
    enum class GizmoMode { Translate, Rotate };

    std::vector<SceneEditor::Object*> s_Objects;
    SceneEditor::Object* s_Selected = nullptr;
    GizmoMode s_GizmoMode = GizmoMode::Translate;
    // rotate gizmo의 ring이 따르는 축. translate는 늘 world의 축이다.
    ImGuizmo::MODE s_RotateAxes = ImGuizmo::LOCAL;
    bool s_ShowPanel = false;

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
            s_Selected->Rotate(camera, s_RotateAxes);
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

    const char* GetGizmoModeName(GizmoMode mode)
    {
        return mode == GizmoMode::Rotate ? "rotate" : "translate";
    }

    const char* GetAxesName(ImGuizmo::MODE axes)
    {
        return axes == ImGuizmo::WORLD ? "world" : "local";
    }

    void ShowPanelContent(void)
    {
        ImGui::Text("Selected: %s", s_Selected != nullptr ? s_Selected->GetName() : "none");
        ImGui::SetItemTooltip("Click an icon in the scene to select it. Click empty space to deselect.");

        ImGui::SeparatorText("Gizmo");
        if (ImGui::RadioButton("Translate (W)", s_GizmoMode == GizmoMode::Translate))
            s_GizmoMode = GizmoMode::Translate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", s_GizmoMode == GizmoMode::Rotate))
            s_GizmoMode = GizmoMode::Rotate;

        ImGui::SeparatorText("Rotate Axes");
        if (ImGui::RadioButton("Local", s_RotateAxes == ImGuizmo::LOCAL))
            s_RotateAxes = ImGuizmo::LOCAL;
        ImGui::SetItemTooltip("The rings turn the object about its own axes.");
        ImGui::SameLine();
        if (ImGui::RadioButton("World", s_RotateAxes == ImGuizmo::WORLD))
            s_RotateAxes = ImGuizmo::WORLD;
        ImGui::SetItemTooltip("The rings turn the object about the world axes.");
    }

    ControlChannel::Fields DescribeObject(const Camera& camera, const SceneEditor::Object& object)
    {
        const Vector3 position = object.GetPosition();
        ControlChannel::Fields fields;
        fields.Object("position", ControlChannel::Fields()
            .Float("x", position.GetX())
            .Float("y", position.GetY())
            .Float("z", position.GetZ()));

        // icon을 그리는 것과 같은 투영이다. camera 뒤에 있으면 화면 위치가 없다.
        ImVec2 screen;
        if (!SceneEditor::ProjectToScene(camera, position, screen))
            return fields.Flag("on_screen", false);

        const RECT scene = ImGuiLayer::GetSceneViewport();
        const bool inScene = screen.x >= scene.left && screen.x < scene.right && screen.y >= scene.top && screen.y < scene.bottom;
        return fields
            .Flag("on_screen", inScene)
            .Object("screen", ControlChannel::Fields().Float("x", screen.x).Float("y", screen.y));
    }

    ControlChannel::Fields DescribeEditor(const Camera& camera)
    {
        ControlChannel::Fields objects;
        for (const SceneEditor::Object* object : s_Objects)
            objects.Object(object->GetName(), DescribeObject(camera, *object));
        return ControlChannel::Fields()
            .Text("selected", s_Selected != nullptr ? s_Selected->GetName() : "none")
            .Text("gizmo_mode", GetGizmoModeName(s_GizmoMode))
            .Text("rotate_axes", GetAxesName(s_RotateAxes))
            .Object("objects", objects);
    }

    bool SelectByName(const std::string& name, std::string& error)
    {
        if (name == "none")
        {
            s_Selected = nullptr;
            return true;
        }

        std::string names;
        for (SceneEditor::Object* object : s_Objects)
        {
            if (name == object->GetName())
            {
                s_Selected = object;
                return true;
            }
            names += std::string(names.empty() ? "" : ", ") + object->GetName();
        }
        error = "no object '" + name + "'. Objects: " + names + ", none";
        return false;
    }

    bool SetGizmoMode(const std::string& name, std::string& error)
    {
        for (GizmoMode mode : { GizmoMode::Translate, GizmoMode::Rotate })
        {
            if (name == GetGizmoModeName(mode))
            {
                s_GizmoMode = mode;
                return true;
            }
        }
        error = "usage: editor mode translate|rotate";
        return false;
    }

    bool SetRotateAxes(const std::string& name, std::string& error)
    {
        for (ImGuizmo::MODE axes : { ImGuizmo::LOCAL, ImGuizmo::WORLD })
        {
            if (name == GetAxesName(axes))
            {
                s_RotateAxes = axes;
                return true;
            }
        }
        error = "usage: editor axes local|world";
        return false;
    }

    void HandleEditorCommand(const std::string& arguments, const ControlChannel::Reply& reply)
    {
        const size_t actionEnd = arguments.find(' ');
        const std::string action = arguments.substr(0, actionEnd);
        const std::string value = actionEnd == std::string::npos ? std::string() : arguments.substr(actionEnd + 1);
        std::string error = "usage: editor select <name>|none | editor mode translate|rotate | editor axes local|world";
        const bool done =
            action == "select" ? SelectByName(value, error) :
            action == "mode" ? SetGizmoMode(value, error) :
            action == "axes" ? SetRotateAxes(value, error) :
            false;
        if (!done)
        {
            reply.Error(error);
            return;
        }
        reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("editor")));
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
    ImGuiLayer::ShowPanel("Scene Editor", s_ShowPanel, ShowPanelContent);

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

void SceneEditor::RegisterCommands(const Camera& camera)
{
    const Camera* projection = &camera;
    ControlChannel::RegisterState("editor", [projection] { return DescribeEditor(*projection); });
    ControlChannel::Register("editor", "editor select <name>|none | editor mode translate|rotate | editor axes local|world: the object the gizmo edits, "
        "the gizmo mode and the axes the rotate rings follow, as clicking an icon, pressing W or E and the Scene Editor panel do. "
        "local turns the object about its own axes and world about the world axes. translate always follows the world axes. "
        "state editor has them, and for each object its world position and, in front of the camera, "
        "its icon's center in client pixels. on_screen is whether that center is in the scene. Icons and the gizmo are drawn only while the UI is shown",
        HandleEditorCommand);
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
