#pragma once

#include "Core/Math/BoundingBox.h"

class GraphicsContext;

namespace Math
{
    class Camera;
}

namespace DynamicGIVolumeBox
{
    void Initialize(void);
    void Draw(GraphicsContext& context, const Math::Camera& camera, const Math::AxisAlignedBox& bounds);
}
