#ifndef __PROBE_VOLUME_HLSLI__
#define __PROBE_VOLUME_HLSLI__

#include "Core/Shaders/IrradianceProbes.hlsli"

struct ProbeDistanceAtlas
{
    Texture2D<float2> moments;
    uint texelsPerSide;
};

ProbeDistanceAtlas MakeProbeDistanceAtlas(Texture2D<float2> moments, uint texelsPerSide)
{
    ProbeDistanceAtlas atlas;
    atlas.moments = moments;
    atlas.texelsPerSide = texelsPerSide;
    return atlas;
}

struct ProbeVolume
{
    IrradianceProbeGrid grid;
    Texture2D<float4> irradianceAtlas;
    ProbeDistanceAtlas distanceAtlas;
    Texture2D<float> backFaceRatios;
};

ProbeVolume MakeProbeVolume(IrradianceProbeGrid grid, Texture2D<float4> irradianceAtlas, ProbeDistanceAtlas distanceAtlas,
    Texture2D<float> backFaceRatios)
{
    ProbeVolume volume;
    volume.grid = grid;
    volume.irradianceAtlas = irradianceAtlas;
    volume.distanceAtlas = distanceAtlas;
    volume.backFaceRatios = backFaceRatios;
    return volume;
}

#endif
