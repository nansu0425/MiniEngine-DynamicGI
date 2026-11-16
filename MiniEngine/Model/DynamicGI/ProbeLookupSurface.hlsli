#ifndef __PROBE_LOOKUP_SURFACE_HLSLI__
#define __PROBE_LOOKUP_SURFACE_HLSLI__

struct ProbeLookupSurface
{
    float3 position;
    float3 geometryNormal;
    float3 shadingNormal;
};

ProbeLookupSurface MakeProbeLookupSurface(float3 position, float3 geometryNormal, float3 shadingNormal)
{
    ProbeLookupSurface surface;
    surface.position = position;
    surface.geometryNormal = geometryNormal;
    surface.shadingNormal = shadingNormal;
    return surface;
}

#endif
