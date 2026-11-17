#pragma once

#include "Core/Math/BoundingBox.h"
#include "Model/DynamicGI/GIFrameInputs.h"
#include "Model/DynamicGI/ProbeWeighting.h"
#include "Model/Renderer/IrradianceProbeGrid.h"

class ColorBuffer;
class ComputeContext;

namespace ProbeLighting
{
    struct Settings
    {
        uint32_t probeCounts[3] = { 16, 8, 8 };
        uint32_t raysPerProbe = 64;
        float hysteresis = 0.95f;
        float distanceSharpness = 1.0f;
        uint32_t distanceTexelsPerSide = 8;
        float backFaceDistanceScale = 0.2f;
        ProbeWeighting::Settings weighting;
    };

    struct Volume
    {
        Math::AxisAlignedBox bounds;
        Settings settings;
    };

    struct Probes
    {
        const IrradianceProbeGrid& grid;
        ColorBuffer& irradiance;
        ColorBuffer& distance;
        ColorBuffer& texelDistance;
        ColorBuffer& backFaceRatio;
        ColorBuffer& offset;
        uint32_t distanceTexelsPerSide;
        ColorBuffer& raySamples;
        const Math::Matrix4& raySamplesRotation;
        uint32_t raysPerProbe;
    };

    struct Progress
    {
        uint32_t updates;
        uint32_t resets;
    };

    void Initialize(void);
    void Shutdown(void);

    void Reset(void);
    Progress GetProgress(void);

    void Update(ComputeContext& context, const Volume& volume, const GIFrameInputs& inputs);
    void EstimateScreenIrradiance(ComputeContext& context, const ProbeWeighting::Settings& weighting, const GIFrameInputs& inputs, uint32_t width, uint32_t height);

    Probes GetProbes(void);
    ColorBuffer& GetScreenIrradiance(void);
}
