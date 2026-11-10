#ifndef __GBUFFER_HLSLI__
#define __GBUFFER_HLSLI__

#include "Model/Renderer/RasterSurface.hlsli"

static const uint kNumGBufferTargets = 5;

struct GBufferTargets
{
    float4 positionAndCoverage : SV_Target0;
    float4 shadingNormalAndMetallic : SV_Target1;
    float4 geometryNormalAndRoughness : SV_Target2;
    float4 baseColor : SV_Target3;
    float4 emissive : SV_Target4;
};

GBufferTargets EncodeGBuffer(RasterSurface surface)
{
    GBufferTargets targets;
    targets.positionAndCoverage = float4(surface.position, 1.0);
    targets.shadingNormalAndMetallic = float4(surface.shadingNormal, surface.metallic);
    targets.geometryNormalAndRoughness = float4(surface.geometryNormal, surface.roughness);
    targets.baseColor = surface.baseColor;
    targets.emissive = float4(surface.emissive, 0.0);
    return targets;
}

bool DecodeGBuffer(float4 targets[kNumGBufferTargets], out RasterSurface surface)
{
    surface.position = targets[0].xyz;
    surface.shadingNormal = targets[1].xyz;
    surface.metallic = targets[1].w;
    surface.geometryNormal = targets[2].xyz;
    surface.roughness = targets[2].w;
    surface.baseColor = targets[3];
    surface.emissive = targets[4].rgb;
    return targets[0].w != 0.0;
}

#endif
