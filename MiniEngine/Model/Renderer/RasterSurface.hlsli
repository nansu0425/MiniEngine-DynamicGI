#ifndef __RASTER_SURFACE_HLSLI__
#define __RASTER_SURFACE_HLSLI__

struct RasterSurface
{
    float3 position;
    float3 geometryNormal;
    float3 shadingNormal;
    float4 baseColor;
    float metallic;
    float roughness;
    float3 emissive;
};

#endif
