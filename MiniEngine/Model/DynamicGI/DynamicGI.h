#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"

class ComputeContext;

namespace DynamicGI
{
    void Initialize(void);
    void Shutdown(void);

    void Reset(void);

    void Update(ComputeContext& context, const GIFrameInputs& inputs, uint32_t width, uint32_t height);
}
