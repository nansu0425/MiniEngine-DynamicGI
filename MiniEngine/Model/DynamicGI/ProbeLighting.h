#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"
#include "Model/Renderer/IrradianceProbeGrid.h"

class ColorBuffer;
class ComputeContext;

namespace ProbeLighting
{
    enum class Visibility { kNone, kShadowRay, kChebyshev };

    struct Settings
    {
        uint32_t probeCounts[3] = { 16, 8, 8 };
        uint32_t raysPerProbe = 64;
        float hysteresis = 0.95f;
        float distanceSharpness = 1.0f;
        uint32_t distanceTexelsPerSide = 8;
        Visibility visibility = Visibility::kNone;
    };

    struct Probes
    {
        const IrradianceProbeGrid& grid;
        ColorBuffer& irradiance;
        ColorBuffer& distance;
        ColorBuffer& texelDistance;
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

    void Update(ComputeContext& context, const Settings& settings, const GIFrameInputs& inputs);
    void EstimateScreenIrradiance(ComputeContext& context, Visibility visibility, const GIFrameInputs& inputs, uint32_t width, uint32_t height);

    Probes GetProbes(void);
    ColorBuffer& GetScreenIrradiance(void);
}
