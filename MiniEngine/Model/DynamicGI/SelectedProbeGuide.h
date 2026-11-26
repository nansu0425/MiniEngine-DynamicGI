#pragma once

#include "Model/DynamicGI/GIProfiling.h"

#include <cstdint>
#include <optional>

class ColorBuffer;
class ComputeContext;

namespace SelectedProbeGuide
{
    void Initialize(void);
    void Shutdown(void);

    void Update(ComputeContext& context, ColorBuffer& probesGuide, std::optional<uint32_t> probe);

    ColorBuffer& GetTexture(void);

    void ListResources(std::vector<GIProfiling::Resource>& resources);
}
