#ifndef __DYNAMIC_GI_VOLUME_BOX_HLSLI__
#define __DYNAMIC_GI_VOLUME_BOX_HLSLI__

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    float3 BoundsMin;
    float Opacity;
    float3 BoundsMax;
    float Padding;
    float3 Color;
};

float4 ProjectBoxCorner(uint corner)
{
    const float3 position = float3(
        (corner & 1) ? BoundsMax.x : BoundsMin.x,
        (corner & 2) ? BoundsMax.y : BoundsMin.y,
        (corner & 4) ? BoundsMax.z : BoundsMin.z);
    return mul(ViewProj, float4(position, 1.0));
}

#endif
