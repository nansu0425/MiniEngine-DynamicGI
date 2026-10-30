// raster의 camera pixel과 DDGI의 probe hit가 쓰는 shading. 함수는 compute shader도 부를 수 있도록 resource와 constant를 인자로 받고,
// pixel shader에서만 쓸 수 있는 기능을 쓰지 않는다.

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

// 아래는 DDGI의 probe ray가 맞은 점 (probe hit)의 shading이다. Majercik 2019의 4.3절은 probe hit를 camera pixel과 같은 shading
// routine으로 shading하고, G3D r7247은 camera pixel의 deferred shader에 probe ray를 위한 switch를 둔다
// (references/G3D-DefaultRenderer_deferredShade.pix:215의 GLOSSY_TO_MATTE). 이 module에서는 아래 함수와 상수가 그 switch다.
// G3D의 code는 가져오지 않고 식만 따랐다.
//
// DDGI의 probe trace와 path tracer의 Shading DDGI Probe Hit가 함께 따르는 계약:
// - probe hit의 material texture는 emission과 normal map까지 kProbeHitTextureLod의 mip으로 읽는다.
// - probe hit가 ray가 온 쪽으로 내는 radiance는 GetProbeHitOutgoingRadiance다. albedo는 GetProbeHitAlbedo다.
// - lightVisibility는 camera pixel처럼 GetDirectionalLightVisibility로 같은 frame의 shadow map에서 구한다. shadow map의 좌표는
//   raster처럼 ShadowMatrix로 옮긴 position이다 (Model/Renderer/DefaultVS.hlsl). 논문은 shadow map을 VSM으로 두지만, camera pixel과
//   같은 routine을 쓴다는 원칙을 따라 raster가 지금 쓰는 조회를 쓴다.
// - GetProbeHitOutgoingRadiance의 irradiance는 probe hit가 directional light 밖의 빛에서 받는 irradiance다. DDGI는 이 irradiance를
//   probe에서 보간해 넘긴다. path tracer는 0을 넘기고, cosine으로 고른 방향의 radiance에 GetProbeHitIndirectWeight를 곱해 path를
//   잇는다. cosine sampling의 pdf가 cos / π라서 곱한 값의 기대값이 GetProbeHitOutgoingRadiance의 irradiance 항이다.

// probe hit가 material texture를 읽는 mip. G3D는 irradiance probe의 ray를 m_diffuseMipLevel 8로 읽는다
// (references/G3D-DefaultRenderer.h:68, references/G3D-DefaultRenderer.cpp:195).
static const float kProbeHitTextureLod = 8.0;

// probe hit가 받는 irradiance에 곱하는 배율. G3D는 probe에서 보간한 irradiance에 m_energyPreservation 0.95를 곱한다
// (references/G3D-DefaultRenderer.h:357, references/G3D-DefaultRenderer.cpp:713, references/G3D-DefaultRenderer_deferredShade.pix:99).
// 그리고 AO가 줄인 빛을 되돌리려고 1.05 x AO를 곱한다 (references/G3D-DefaultRenderer_deferredShade.pix:153). probe ray를 shading할
// 때는 AO를 끄므로 (references/G3D-DefaultRenderer.cpp:686) AO는 1이다. AO가 1이라는 것은 code를 읽어 한 추론이다.
static const float kProbeHitIndirectScale = 0.95 * 1.05;

// glTF spec과 다르다: probe hit는 glTF의 metallic-roughness BRDF 대신 이 albedo로 반사하는 Lambert로 shading한다. G3D는 probe hit에서
// glossy 반사를 matte 반사에 합쳐 lambertian x (1 - F0) + F0를 albedo로 쓴다 (references/G3D-DefaultRenderer_deferredShade.pix:216.
// F0는 수직 입사의 Fresnel 반사율). glTF로 옮기면서 dielectric은 lambertian을 baseColor, F0를 0.04로 두었다
// (references/glTF/specification/2.0/Specification.adoc:3825). metal은 lambertian을 0, F0를 baseColor로 두었다 (같은 파일:3821).
// 두 albedo를 metallic으로 섞는 것은 spec이 두 BRDF를 metallic으로 섞는 것을 albedo에 옮긴 이 repo의 전제이고, G3D의 식이 아니다.
float3 GetProbeHitAlbedo(float3 baseColor, float metallic)
{
    static const float kDielectricF0 = 0.04;
    const float3 dielectric = baseColor * (1.0 - kDielectricF0) + kDielectricF0;
    const float3 metal = baseColor;
    return lerp(dielectric, metal, metallic);
}

// probe hit가 directional light 밖의 빛을 반사하는 비율. π로 나눈 값이 그 빛의 irradiance를 radiance로 바꾼다. path tracer는 이 값을
// cosine으로 고른 방향의 radiance에 곱한다.
float3 GetProbeHitIndirectWeight(float3 albedo)
{
    return albedo * kProbeHitIndirectScale;
}

// probe hit가 ray가 온 쪽으로 내는 radiance. emission과, directional light와 irradiance를 albedo의 Lambert로 반사한 빛을 더한다.
// albedo는 GetProbeHitAlbedo이고, irradiance는 위의 계약대로 directional light 밖의 빛에서 받는 irradiance다. 나머지 인자의 뜻은
// GetOutgoingRadiance와 같다. Lambert의 반사는 view 방향과 무관하므로 view를 받지 않는다.
float3 GetProbeHitOutgoingRadiance(float3 albedo, float3 emissive, float3 normal, float3 lightDirection, float3 lightIlluminance,
    float lightVisibility, float3 irradiance)
{
    // illuminance는 light에 수직인 면의 값이므로 normal과의 cos를 곱한다.
    return emissive + albedo * kInversePi * lightIlluminance * (max(0.0, dot(normal, lightDirection)) * lightVisibility) +
        GetProbeHitIndirectWeight(albedo) * kInversePi * irradiance;
}

#endif
