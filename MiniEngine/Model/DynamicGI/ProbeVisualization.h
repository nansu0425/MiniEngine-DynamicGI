#pragma once

#include "Model/DynamicGI/ProbeLighting.h"

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
        bool showTexelDistances = true;
        bool showRaySamples = true;
    };

    void Initialize(void);

    void DrawSpheres(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
        const ProbeLighting::Probes& probes, float radiusPerSpacing);

    std::optional<uint32_t> FindProbeUnderCursor(const Math::Camera& camera, const IrradianceProbeGrid& grid, float radiusPerSpacing);

    void DrawDistanceLines(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
        const ProbeLighting::Probes& probes, uint32_t probe, const Settings& settings);
}
