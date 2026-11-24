#pragma once

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
}
