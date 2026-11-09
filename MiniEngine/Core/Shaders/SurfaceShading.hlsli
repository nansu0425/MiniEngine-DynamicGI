// raster의 camera pixel과 indirect hit가 쓰는 shading. 함수는 compute shader도 부를 수 있도록 resource와 constant를 인자로 받고,
// pixel shader에서만 쓸 수 있는 기능을 쓰지 않는다.

#ifndef __SURFACE_SHADING_HLSLI__
#define __SURFACE_SHADING_HLSLI__

#include "Core/Shaders/GltfBrdf.hlsli"

// directional light가 shading하는 점에 닿는 비율. 면이 light를 가리는지는 docs/policy.md의 "camera 밖에서 본 doubleSided"가 정한다.
// shadowCoord는 shadow map의 uv와 비교할 depth다. geometryNormal은 shading하는 점이 놓인 면의 normal이고, 점을 보는 쪽을 향한다.
// geometryNormal은 방향만 쓰므로 길이가 1이 아니어도 된다.
float GetDirectionalLightVisibility(Texture2D<float> shadowMap, SamplerComparisonState shadowMapSampler, float3 shadowCoord,
    float3 geometryNormal, float3 lightDirection)
{
    float visibility = shadowMap.SampleCmpLevelZero(shadowMapSampler, shadowCoord.xy, shadowCoord.z);
    if (dot(geometryNormal, lightDirection) <= 0.0)
        visibility = 0.0;
    return visibility;
}

#endif
