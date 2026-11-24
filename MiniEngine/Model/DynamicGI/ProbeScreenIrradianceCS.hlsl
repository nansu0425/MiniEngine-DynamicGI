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

Texture2D<float4> ProbesIrradiance : register(t6);
Texture2D<float2> ProbesDistance : register(t7);
Texture2D<float> ProbesBackFaceRatio : register(t8);
Texture2D<float4> ProbesOffset : register(t9);
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

    const ProbeDistanceAtlas distanceAtlas = MakeProbeDistanceAtlas(ProbesDistance, DistanceTexelsPerSide);
    const ProbeVolume volume = MakeProbeVolume(MakeProbePositions(Grid, ProbesOffset), ProbesIrradiance, distanceAtlas, ProbesBackFaceRatio);
    const ProbeLookupSurface lookupSurface = MakeProbeLookupSurface(surface.position, surface.geometryNormal, surface.shadingNormal);
    ScreenIrradiance[DTid] = float4(SampleWeightedIrradianceProbes(Weighting, volume, LinearClamp, lookupSurface), 0.0);
}
