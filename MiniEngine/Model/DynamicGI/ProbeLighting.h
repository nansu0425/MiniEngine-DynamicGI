#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"
#include "Model/Renderer/IrradianceProbeGrid.h"

class ColorBuffer;
class ComputeContext;

namespace ProbeLighting
{
    enum class Visibility { kNone, kShadowRay };

    struct Settings
    {
        uint32_t probeCounts[3] = { 16, 8, 8 };
        uint32_t raysPerProbe = 64;
        float hysteresis = 0.95f;
        Visibility visibility = Visibility::kNone;
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
    void EstimateScreenIrradiance(ComputeContext& context, const GIFrameInputs& inputs, uint32_t width, uint32_t height);

    ColorBuffer& GetProbesIrradiance(void);
    ColorBuffer& GetRayRadiance(void);
    ColorBuffer& GetScreenIrradiance(void);
    const IrradianceProbeGrid& GetGrid(void);
}
