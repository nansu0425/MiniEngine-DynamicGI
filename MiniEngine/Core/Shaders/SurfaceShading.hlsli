// raster의 camera pixel이 쓰는 shading. 함수는 compute shader도 부를 수 있도록 resource와 constant를 인자로 받고, pixel shader에서만
// 쓸 수 있는 기능을 쓰지 않는다.

#ifndef __SURFACE_SHADING_HLSLI__
#define __SURFACE_SHADING_HLSLI__

#include "Core/Shaders/GltfBrdf.hlsli"

// shading하는 점의 surface 값. baseColor, metallic, roughness, emissive는 glTF material의 값이다.
struct SurfaceShadingInput
{
    // BRDF를 평가하는 단위 normal. double-sided material의 뒷면에서는 뒤집은 normal이다.
    float3 normal;
    float3 baseColor;
    float metallic;
    float roughness;
    float3 emissive;
};

// directional light가 shading하는 점에 닿는 비율. 면이 light를 가리는지는 README.md의 "camera 밖에서 본 doubleSided"가 정한다.
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

// shading하는 점에서 view 쪽으로 나가는 radiance. emission과, directional light를 glTF BRDF로 반사한 빛을 더한다.
// view는 점에서 viewer 쪽을, lightDirection은 점에서 light 쪽을 향하는 단위 vector다. lightIlluminance는 light에 수직인 면의
// illuminance이고, lightVisibility는 directional light가 점에 닿는 비율이다.
float3 GetOutgoingRadiance(SurfaceShadingInput surface, float3 view, float3 lightDirection, float3 lightIlluminance, float lightVisibility)
{
    // illuminance는 light에 수직인 면의 값이므로 normal과의 cos를 곱한다.
    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor, surface.metallic, surface.roughness, surface.normal, view);
    return surface.emissive + EvaluateGltfBrdf(brdf, lightDirection) * lightIlluminance *
        (max(0.0, dot(surface.normal, lightDirection)) * lightVisibility);
}

#endif
