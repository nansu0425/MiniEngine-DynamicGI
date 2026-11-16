#pragma once

#include "Model/DynamicGI/ProbeContribution.h"
#include "Model/DynamicGI/ProbeLighting.h"

#include <optional>

class ColorBuffer;
class GraphicsContext;
class StructuredBuffer;

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
        const ProbeLighting::Probes& probes, const ProbeWeighting::Settings& weighting, float radiusPerSpacing);

    std::optional<uint32_t> FindProbeUnderCursor(const Math::Camera& camera, const IrradianceProbeGrid& grid, float radiusPerSpacing);

    void DrawDistanceLines(GraphicsContext& context, const Math::Camera& camera, const ProbeLighting::Probes& probes, uint32_t probe,
        const Settings& settings, std::optional<uint32_t> highlightedRay);

    void DrawContribution(GraphicsContext& context, const Math::Camera& camera, const IrradianceProbeGrid& grid, StructuredBuffer& contribution,
        std::optional<uint32_t> hoveredCorner);

    void AnnotateContribution(const Math::Camera& camera, const IrradianceProbeGrid& grid, const ProbeContribution::Surface& contribution,
        std::optional<uint32_t> hoveredCorner);
}
