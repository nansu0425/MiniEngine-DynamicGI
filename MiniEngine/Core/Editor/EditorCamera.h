#pragma once

#include "Core/Camera/CameraController.h"

// Unreal, Unity editor의 viewport camera처럼 mouse와 keyboard로 조작한다.
// 오른쪽 button을 누른 채 mouse로 둘러보고 WASD, QE로 움직이며 wheel로 이동 속도를 바꾼다.
// 가운데 button을 누른 채 끌면 화면과 평행하게 움직인다.
// 이동 속도는 user.ini에 저장되어 다음 실행 때 되돌아온다. 위치와 방향은 저장하지 않고 app이 scene 마다 정한다.
class EditorCamera : public CameraController
{
public:
    EditorCamera(Math::Camera& camera, Math::Vector3 worldUp);
    virtual ~EditorCamera();

    virtual void Update(float deltaTime) override;

    // heading은 world up을 축으로 돈 각도, pitch는 수평에서 올려다본 각도다.
    struct Pose
    {
        Math::Vector3 position;
        float heading;
        float pitch;
    };
    Pose GetPose() const;
    // Update를 기다리지 않고 camera에 바로 적용해, 같은 frame에 그리는 것부터 새 pose로 보게 한다.
    void SetPose(const Pose& pose);
    // 위치는 그대로 두고 target을 보게 SetPose한다. 바로 위나 아래를 보면 heading이 정해지지 않으므로 그대로 둔다.
    // target은 camera 위치와 달라야 한다.
    void LookAt(Math::Vector3 target);
    float GetMoveSpeed() const { return m_MoveSpeed; }

private:
    Math::Matrix3 GetOrientation() const;

    Math::Vector3 m_WorldUp;
    Math::Vector3 m_WorldNorth;
    Math::Vector3 m_WorldEast;

    float m_Heading;
    float m_Pitch;
    float m_MoveSpeed;
};
