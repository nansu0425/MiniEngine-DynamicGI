#ifndef __PROBE_VOLUME_HLSLI__
#define __PROBE_VOLUME_HLSLI__

#include "Model/DynamicGI/ProbePositions.hlsli"

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
    ProbePositions positions;
    Texture2D<float4> irradianceAtlas;
    ProbeDistanceAtlas distanceAtlas;
    Texture2D<float> backFaceRatios;
};

ProbeVolume MakeProbeVolume(ProbePositions positions, Texture2D<float4> irradianceAtlas, ProbeDistanceAtlas distanceAtlas,
    Texture2D<float> backFaceRatios)
{
    ProbeVolume volume;
    volume.positions = positions;
    volume.irradianceAtlas = irradianceAtlas;
    volume.distanceAtlas = distanceAtlas;
    volume.backFaceRatios = backFaceRatios;
    return volume;
}

#endif
