#ifndef __GBUFFER_TEXTURES_HLSLI__
#define __GBUFFER_TEXTURES_HLSLI__

#include "Model/Renderer/GBuffer.hlsli"

Texture2D<float4> GBufferTextures[kNumGBufferTargets] : register(t0);

bool LoadGBuffer(uint2 pixel, out RasterSurface surface)
{
    float4 targets[kNumGBufferTargets];
    [unroll]
    for (uint i = 0; i < kNumGBufferTargets; ++i)
        targets[i] = GBufferTextures[i][pixel];
    return DecodeGBuffer(targets, surface);
}

#endif
