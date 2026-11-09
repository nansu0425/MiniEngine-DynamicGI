#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"

class ComputeContext;
class StructuredBuffer;

// pixel마다 raster의 sample 위치로 쏜 camera ray가 처음 맞은 점 (GISurface.hlsli의 GISurface). TAA jitter를 따르도록 frame마다 다시 구한다.
namespace GISurface
{
    void Initialize(void);
    void Shutdown(void);

    void Update(ComputeContext& context, const GIFrameInputs& inputs, uint32_t width, uint32_t height);
    // Update가 채운 buffer. 원소의 순서는 GISurface.hlsli의 GetGISurfaceIndex다.
    StructuredBuffer& GetSurfaces(void);
}
