#include "Model/Renderer/GBufferTextures.hlsli"
#include "Model/DynamicGI/ProbeWeighting.hlsli"

cbuffer Constants : register(b0)
{
    uint Width;
    uint Height;
    uint DistanceTexelsPerSide;
    ProbeWeighting Weighting;
    IrradianceProbeGrid Grid;
};

Texture2D<float4> ProbesIrradiance : register(t5);
Texture2D<float2> ProbesDistance : register(t6);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> ScreenIrradiance : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height)
        return;

    RasterSurface surface;
    if (!LoadGBuffer(DTid, surface))
        return;

    ScreenIrradiance[DTid] = float4(SampleWeightedIrradianceProbes(Weighting, ProbesIrradiance, MakeProbeDistanceAtlas(ProbesDistance, DistanceTexelsPerSide), LinearClamp, Grid,
        MakeProbeLookupSurface(surface.position, surface.geometryNormal, surface.shadingNormal)), 0.0);
}
