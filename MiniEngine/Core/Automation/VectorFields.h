#pragma once

#include "Core/Automation/ControlChannel.h"
#include "Core/Math/Vector.h"

namespace ControlChannel
{
    inline Fields DescribeVector(Math::Vector3 vector)
    {
        return Fields().Float("x", vector.GetX()).Float("y", vector.GetY()).Float("z", vector.GetZ());
    }
}
