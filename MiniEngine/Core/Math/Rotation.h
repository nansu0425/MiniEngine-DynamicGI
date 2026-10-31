#pragma once

#include "Core/Math/Matrix3.h"

// rotation을 Euler 각과 quaternion으로 나타내는 변환. 여러 기능이 같은 규약으로 rotation을 읽고 쓰도록 변환은 Rotation.h에만 둔다.
namespace Math
{
    // Euler 각 x, y (radian)가 나타내는 rotation. 순서는 intrinsic Y-X다. base를 자기 +Y 축으로 y만큼 돌린 뒤, 돌린 base를 다시 자기
    // +X 축으로 x만큼 돌린다.
    INLINE Matrix3 MakeEulerRotation(const Matrix3& base, float x, float y)
    {
        return base * Matrix3::MakeYRotation(y) * Matrix3::MakeXRotation(x);
    }

    // glTF node의 rotation처럼 x, y, z, w 순서로 받은 quaternion을 normalize해 rotation에 담는다. 길이가 0인 quaternion은 rotation을
    // 나타내지 않으므로 false를 돌려준다.
    INLINE bool MakeQuaternionRotation(float x, float y, float z, float w, Quaternion& rotation)
    {
        const Quaternion given(XMVectorSet(x, y, z, w));
        if (XMVectorGetX(XMQuaternionLengthSq(given)) == 0.0f)
            return false;
        rotation = Normalize(given);
        return true;
    }

    // rotation을 quaternion으로 적을 때의 형태. q와 -q는 같은 rotation이므로 w가 0 이상인 쪽을 적는다.
    INLINE Quaternion GetCanonicalQuaternion(Quaternion rotation)
    {
        return XMVectorGetW(rotation) < 0.0f ? -rotation : rotation;
    }
}
