#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"

class ComputeContext;
class GraphicsContext;

namespace DynamicGI
{
    void Initialize(void);
    void Shutdown(void);

    void Reset(void);

    void Update(ComputeContext& context, const GIFrameInputs& inputs, uint32_t width, uint32_t height);
    void UpdateProbeSelection(const Math::Camera& camera);
    void DrawProbes(GraphicsContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
}
