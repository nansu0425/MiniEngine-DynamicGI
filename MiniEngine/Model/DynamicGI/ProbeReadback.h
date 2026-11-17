#pragma once

#include "Model/Renderer/IrradianceProbeGrid.h"

namespace ProbeReadback
{
    void Shutdown(void);

    void Update(void);

    Math::Vector3 GetProbePosition(const IrradianceProbeGrid& grid, uint32_t probe);
}
