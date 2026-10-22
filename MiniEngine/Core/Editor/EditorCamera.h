#pragma once

#include "Core/Camera/CameraController.h"

// Unreal, Unity editor의 viewport camera처럼 mouse와 keyboard로 조작한다.
// 오른쪽 button을 누른 채 mouse로 둘러보고 WASD, QE로 움직이며 wheel로 이동 속도를 바꾼다.
// 가운데 button을 누른 채 끌면 화면과 평행하게 움직인다.
// 위치, 방향, 이동 속도는 editor.ini에 저장되어 다음 실행 때 되돌아온다.
class EditorCamera : public CameraController
{
public:
    EditorCamera(Math::Camera& camera, Math::Vector3 worldUp);
    virtual ~EditorCamera();

    virtual void Update(float deltaTime) override;

private:
    Math::Matrix3 GetOrientation() const;

    Math::Vector3 m_WorldUp;
    Math::Vector3 m_WorldNorth;
    Math::Vector3 m_WorldEast;

    float m_Heading;
    float m_Pitch;
    float m_MoveSpeed;
};
