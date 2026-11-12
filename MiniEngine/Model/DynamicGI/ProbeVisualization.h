#pragma once

#include "Model/Renderer/IrradianceProbeGrid.h"

class ColorBuffer;
class GraphicsContext;

namespace Math
{
    class Camera;
}

namespace ProbeVisualization
{
    struct Settings
    {
        bool show = false;
        float radiusPerSpacing = 0.15f;
    };

    void Initialize(void);

    void DrawSpheres(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
        ColorBuffer& irradiance, const IrradianceProbeGrid& grid, float radiusPerSpacing);
}
