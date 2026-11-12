#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"
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

    ColorBuffer& GetIrradiance(void);
    ColorBuffer& GetRayRadiance(void);
    const IrradianceProbeGrid& GetGrid(void);
}
