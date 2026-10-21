#include "Core/pch.h"
#include "Core/Editor/EditorCamera.h"
#include "Core/Camera/Camera.h"
#include "Core/App/GameInput.h"

using namespace Math;

namespace
{
    // wheel 한 칸마다 이동 속도에 곱하거나 나누는 값
    constexpr float kMoveSpeedStep = 1.25f;
    constexpr float kMinMoveSpeed = 10.0f;
    constexpr float kMaxMoveSpeed = 100000.0f;

    float GetKeyAxis(GameInput::DigitalInput positive, GameInput::DigitalInput negative)
    {
        return (GameInput::IsPressed(positive) ? 1.0f : 0.0f) - (GameInput::IsPressed(negative) ? 1.0f : 0.0f);
    }
}

EditorCamera::EditorCamera(Camera& camera, Vector3 worldUp) : CameraController(camera)
{
    m_WorldUp = Normalize(worldUp);
    m_WorldNorth = Normalize(Cross(m_WorldUp, Vector3(kXUnitVector)));
    m_WorldEast = Cross(m_WorldNorth, m_WorldUp);

    m_Pitch = ASin(Clamp((float)Dot(camera.GetForwardVec(), m_WorldUp), -1.0f, 1.0f));
    const Vector3 forward = Normalize(Cross(m_WorldUp, camera.GetRightVec()));
    m_Heading = ATan2(-Dot(forward, m_WorldEast), Dot(forward, m_WorldNorth));

    // 처음 속도는 upstream의 FlyingFPSCamera와 같다.
    m_MoveSpeed = 1000.0f;
}

void EditorCamera::Update(float deltaTime)
{
    const float mouseX = GameInput::GetAnalogInput(GameInput::kAnalogMouseX);
    const float mouseY = GameInput::GetAnalogInput(GameInput::kAnalogMouseY);
    const bool looking = GameInput::IsPressed(GameInput::kMouse1);
    const bool panning = !looking && GameInput::IsPressed(GameInput::kMouse2);

    if (looking)
    {
        m_Heading = XMScalarModAngle(m_Heading - mouseX);
        m_Pitch = Clamp(m_Pitch + mouseY, -XM_PIDIV2, XM_PIDIV2);

        const float scroll = GameInput::GetAnalogInput(GameInput::kAnalogMouseScroll);
        m_MoveSpeed = Clamp(m_MoveSpeed * powf(kMoveSpeedStep, scroll), kMinMoveSpeed, kMaxMoveSpeed);
    }

    const Matrix3 orientation =
        Matrix3(m_WorldEast, m_WorldUp, -m_WorldNorth) *
        Matrix3::MakeYRotation(m_Heading) *
        Matrix3::MakeXRotation(m_Pitch);

    Vector3 position = m_TargetCamera.GetPosition();
    if (looking)
    {
        // WASD는 camera가 보는 방향을 따르고, E와 Q는 어디를 보든 world 기준으로 위아래로 움직인다.
        const float step = m_MoveSpeed * deltaTime;
        const Vector3 cameraMove(
            GetKeyAxis(GameInput::kKey_d, GameInput::kKey_a) * step,
            0.0f,
            GetKeyAxis(GameInput::kKey_s, GameInput::kKey_w) * step);
        const float worldUpMove = GetKeyAxis(GameInput::kKey_e, GameInput::kKey_q) * step;
        position += orientation * cameraMove + m_WorldUp * worldUpMove;
    }
    else if (panning)
    {
        // 화면을 잡아 끄는 것처럼 camera는 mouse와 반대로 움직이고, 움직이는 거리는 이동 속도에 비례한다.
        position += orientation * Vector3(-mouseX * m_MoveSpeed, -mouseY * m_MoveSpeed, 0.0f);
    }

    m_TargetCamera.SetTransform(AffineTransform(orientation, position));
    m_TargetCamera.Update();
}
