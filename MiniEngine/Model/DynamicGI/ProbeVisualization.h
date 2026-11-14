#pragma once

#include "Model/Renderer/IrradianceProbeGrid.h"

#include <optional>

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
        bool showTexelMeans = true;
        bool showRaySamples = true;
    };

    void Initialize(void);

    void DrawSpheres(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
        ColorBuffer& irradiance, const IrradianceProbeGrid& grid, float radiusPerSpacing);

    std::optional<uint32_t> FindProbeUnderCursor(const Math::Camera& camera, const IrradianceProbeGrid& grid, float radiusPerSpacing);

    void DrawDistanceLines(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
        ColorBuffer& probesDistance, ColorBuffer& raySamples, const Math::Matrix4& raySamplesRotation, const IrradianceProbeGrid& grid,
        uint32_t raysPerProbe, uint32_t probe, const Settings& settings);
}
