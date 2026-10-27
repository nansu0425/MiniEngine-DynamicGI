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
    bool s_ShowHierarchy = false;
    bool s_ShowDetails = false;

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

    void EditSelected(const Camera& camera, SceneEditor::IconObject& selected)
    {
        if (s_GizmoMode == GizmoMode::Rotate)
        {
            selected.Rotate(camera, s_RotateAxes);
            return;
        }

        Matrix4 transform(AffineTransform(selected.GetPosition()));
        if (SceneEditor::Manipulate(camera, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, transform))
            selected.SetPosition(Vector3(transform.GetW()));
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
            const SceneEditor::IconObject* iconObject = object->AsIconObject();
            if (iconObject == nullptr || !SceneEditor::ProjectToScene(camera, iconObject->GetPosition(), icon))
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

    void ShowGizmoToolbar(void)
    {
        if (ImGui::RadioButton("Translate (W)", s_GizmoMode == GizmoMode::Translate))
            s_GizmoMode = GizmoMode::Translate;
        ImGui::SetItemTooltip("Moves the selected object along the world axes.");
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", s_GizmoMode == GizmoMode::Rotate))
            s_GizmoMode = GizmoMode::Rotate;

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", s_RotateAxes == ImGuizmo::LOCAL))
            s_RotateAxes = ImGuizmo::LOCAL;
        ImGui::SetItemTooltip("The rotate rings turn the object about its own axes.");
        ImGui::SameLine();
        if (ImGui::RadioButton("World", s_RotateAxes == ImGuizmo::WORLD))
            s_RotateAxes = ImGuizmo::WORLD;
        ImGui::SetItemTooltip("The rotate rings turn the object about the world axes.");
    }

    void ShowHierarchyContent(void)
    {
        for (SceneEditor::Object* object : s_Objects)
        {
            if (ImGui::Selectable(object->GetName(), object == s_Selected))
                s_Selected = object;
        }
    }

    void ShowDetailsContent(void)
    {
        if (s_Selected == nullptr)
        {
            ImGui::TextDisabled("Select an object in the Scene Hierarchy or click its icon in the scene.");
            return;
        }

        ImGui::SeparatorText(s_Selected->GetName());
        // 물체마다 Color처럼 같은 label의 widget이 있으므로, 고른 물체가 바뀌면 ImGui가 다른 widget으로 보게 한다.
        ImGui::PushID(s_Selected);
        s_Selected->ShowDetails();
        ImGui::PopID();
    }

    ControlChannel::Fields DescribeIcon(const Camera& camera, const SceneEditor::IconObject& object)
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
        for (SceneEditor::Object* object : s_Objects)
        {
            const SceneEditor::IconObject* iconObject = object->AsIconObject();
            objects.Object(object->GetName(), iconObject != nullptr ? DescribeIcon(camera, *iconObject) : ControlChannel::Fields());
        }
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
    ImGuiLayer::ShowPanel("Scene Hierarchy", s_ShowHierarchy, ShowHierarchyContent);
    ImGuiLayer::ShowPanel("Details", s_ShowDetails, ShowDetailsContent);

    if (!ImGuiLayer::BeginSceneGizmo())
        return;

    UpdateGizmoMode();

    for (Object* object : s_Objects)
    {
        if (const IconObject* iconObject = object->AsIconObject())
            iconObject->Draw(camera, object == s_Selected);
    }

    if (s_Selected != nullptr && s_Selected->AsIconObject() != nullptr)
        EditSelected(camera, *s_Selected->AsIconObject());

    // gizmo나 다른 창 위의 click은 ImGui가 mouse를 가져가므로 물체를 고르는 데 쓰지 않는다.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse &&
        !ImGuizmo::IsUsingAny() && !GameInput::IsMouseCapturedByCamera())
    {
        s_Selected = FindObjectUnderCursor(camera);
    }

    ImGuiLayer::EndSceneGizmo();

    // gizmo 창보다 나중에 만들어야 icon과 gizmo 위에 보인다.
    if (ImGuiLayer::BeginSceneToolbar())
    {
        ShowGizmoToolbar();
        ImGuiLayer::EndSceneToolbar();
    }
}

void SceneEditor::RegisterCommands(const Camera& camera)
{
    const Camera* projection = &camera;
    ControlChannel::RegisterState("editor", [projection] { return DescribeEditor(*projection); });
    ControlChannel::Register("editor", "editor select <name>|none | editor mode translate|rotate | editor axes local|world: the selected object, "
        "whose settings the Details panel shows and which the gizmo edits, the gizmo mode and the axes the rotate rings follow, "
        "as clicking an object in the Scene Hierarchy panel or its icon, pressing W or E and the toolbar at the top right of the scene do. "
        "local turns the object about its own axes and world about the world axes. translate always follows the world axes. "
        "state editor has them, and the objects in the order of the Scene Hierarchy panel, each with its world position and, in front of the camera, "
        "its icon's center in client pixels. on_screen is whether that center is in the scene. An object without an icon, "
        "such as the Sky Light or the model, has neither and no gizmo. Icons and the gizmo are drawn only while the UI is shown",
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
