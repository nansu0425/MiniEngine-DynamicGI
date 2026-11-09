#ifndef __GI_SURFACE_HLSLI__
#define __GI_SURFACE_HLSLI__

// raster의 sample 위치로 쏜 primary ray에 맞은 surface의 정보
struct GISurface
{
    float3 position;
    uint hit;
    float3 geometryNormal;
    float roughness;
    float3 shadingNormal;
    float metallic;
    float3 view;
    float3 baseColor;
};

#endif
