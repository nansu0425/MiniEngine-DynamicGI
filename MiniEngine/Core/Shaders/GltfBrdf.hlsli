/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

// glTF 2.0 metallic-roughness material의 BRDF (references/glTF의 Specification.adoc Appendix B). path tracer와 raster가 같은 함수로
// 평가한다. scripts/gltfbrdf.py가 같은 BRDF를 float64로 계산하고, scripts/check-gltf-brdf.py가 GPU에서 이 함수를 돌려 그 값과 견준다.
// Capsaicin (references/manifest.json의 commit)의 src/core/src/materials/material_sampling.hlsl에서 sampleGGXVNDFSphericalCap을 가져왔다.
// 고친 점: 이름을 SampleVisibleMicrofacetNormal로 바꿨고 2 * PI를 풀어 썼다.

#ifndef __GLTF_BRDF_HLSLI__
#define __GLTF_BRDF_HLSLI__

#include "Core/Shaders/GltfBrdfTables.hlsli"
#include "Core/Shaders/LambertBrdf.hlsli"

static const float kInversePi = 0.318309886183790671538;
static const float3 kRec709Luminance = float3(0.2126, 0.7152, 0.0722);

// --- coupling table --------------------------------------------------------------------------
// node의 위치와 보간은 scripts/gltfbrdf.py의 Tables와 같다.

void FindTableInterval(float coordinate, uint count, out uint index, out float fraction)
{
    const float position = saturate(coordinate) * (count - 1);
    index = min((uint)position, count - 2);
    fraction = position - index;
}

// α의 node는 log α에서 고르게 놓여, kGltfMinAlpha가 0이고 1이 1이다.
void FindRoughnessInterval(float alpha, out uint index, out float fraction)
{
    FindTableInterval(1.0 - log2(alpha) / log2(kGltfMinAlpha), kSpecularAlbedoRoughnessNodes, index, fraction);
}

// f0가 kGltfDielectricF0인 specular lobe의 directional albedo E_s(μ). μ의 node는 μ의 세제곱근에서 고르게 놓였다.
float GetDielectricSpecularAlbedo(float cosine, float alpha)
{
    uint row, column;
    float rowFraction, columnFraction;
    FindRoughnessInterval(alpha, row, rowFraction);
    FindTableInterval(pow(saturate(cosine), 1.0 / 3.0), kSpecularAlbedoCosineNodes, column, columnFraction);
    const uint low = row * kSpecularAlbedoCosineNodes + column;
    const uint high = low + kSpecularAlbedoCosineNodes;
    return lerp(lerp(kDielectricSpecularAlbedo[low], kDielectricSpecularAlbedo[low + 1], columnFraction),
        lerp(kDielectricSpecularAlbedo[high], kDielectricSpecularAlbedo[high + 1], columnFraction), rowFraction);
}

// E_s의 cosine-weighted 평균 E_avg = 2∫E_s(μ) μ dμ
float GetAverageDielectricSpecularAlbedo(float alpha)
{
    uint row;
    float rowFraction;
    FindRoughnessInterval(alpha, row, rowFraction);
    return lerp(kAverageDielectricSpecularAlbedo[row], kAverageDielectricSpecularAlbedo[row + 1], rowFraction);
}

// --- BRDF ------------------------------------------------------------------------------------

// 한 점에서 view 쪽으로 반사하는 빛을 구할 때 light 방향과 무관한 값. 점마다 한 번 만들어 그 점의 직접광과 다음 방향 고르기에 같이 쓴다.
struct GltfBrdfInput
{
    float3 normal;
    float3 view;
    float viewCosine;
    float alpha;
    float3 specularF0;
    // coupled diffuse의 f에서 light 쪽의 (1 − E_s(μ_i))를 뺀 몫
    float3 diffuseFactor;
    // 다음 방향을 specular lobe에서 고를 확률
    float specularProbability;
};

float3 GetSchlickFresnel(float3 f0, float cosine)
{
    const float weight = 1.0 - cosine;
    const float weightSquared = weight * weight;
    return f0 + (1.0 - f0) * (weightSquared * weightSquared * weight);
}

// D (spec :3697). halfway는 정규화하지 않은 v + l이다. 분모의 (n·h)²(α² − 1) + 1 = sin²θ_h + α² cos²θ_h를 n × h와 n·h로 구한다.
// 1 − (n·h)²로 구하면 roughness가 작은 specular peak 근처에서 float32가 sin²θ_h를 지운다.
float GetMicrofacetDistribution(float3 normal, float3 halfway, float alpha)
{
    const float3 across = cross(normal, halfway);
    const float along = dot(normal, halfway);
    if (along <= 0.0)
        return 0.0;
    const float alphaSquared = alpha * alpha;
    const float scale = dot(halfway, halfway) / (dot(across, across) + alphaSquared * along * along);
    return alphaSquared * kInversePi * scale * scale;
}

// height-correlated Smith의 V (spec :3727). v와 l이 둘 다 위쪽이면 χ⁺(H·L)와 χ⁺(H·V)는 1이다.
float GetVisibility(float viewCosine, float lightCosine, float alpha)
{
    const float alphaSquared = alpha * alpha;
    return 0.5 / (viewCosine * sqrt(alphaSquared + (1.0 - alphaSquared) * lightCosine * lightCosine) +
        lightCosine * sqrt(alphaSquared + (1.0 - alphaSquared) * viewCosine * viewCosine));
}

// baseColor, metallic, roughness는 glTF의 값이다. normal과 view는 단위 vector이고 view는 그 점에서 바라보는 쪽을 향한다.
GltfBrdfInput MakeGltfBrdfInput(float3 baseColor, float metallic, float roughness, float3 normal, float3 view)
{
    metallic = saturate(metallic);
    roughness = saturate(roughness);

    GltfBrdfInput input;
    input.normal = normal;
    // n·v ≤ 0은 normal map이 shading normal을 view에서 등지게 기울인 곳이다 (path와 camera는 geometric normal 앞에서 온다).
    // spec의 식은 이 경우를 정하지 않으므로 v를 shading normal의 접평면에 대해 거울로 옮겨 그 v의 BRDF로 평가하고 고른다.
    // energy 보존과 평가와 고르기의 일치는 n·v > 0의 BRDF에서 그대로 따라오고, n·v = 0에서 이어진다. 0을 돌려주면 그런 곳이 검게 되어
    // energy를 잃는다. sample implementation처럼 |N·V|만 쓰고 h를 원래 v로 구하면, 이 coupling과 함께 baseColor 1인 dielectric이
    // α = 1, n·v = −0.03 ~ −0.6에서 1.004 ~ 1.016을 반사하고 α = 0.01에서는 specular를 거의 잃는다 (이 작업을 할 때 계산).
    input.view = view - 2.0 * min(0.0, dot(normal, view)) * normal;
    input.viewCosine = saturate(dot(normal, input.view));
    // α = 0이면 D가 delta가 되어 식으로 평가할 수 없으므로 하한을 둔다 (spec :3741). 하한의 D 최대값 1 / (π α²) ≈ 3.2e5는 float32에서
    // 안전하고, 이 α에서도 peak 근처의 D가 float32에서 상대 1e-3 안에 든다 (scripts/check-gltf-brdf.py의 [B1]).
    input.alpha = max(roughness * roughness, kGltfMinAlpha);
    // Schlick의 F는 f0의 1차식이라 mix(dielectric, metal, metallic)의 두 specular는 f0를 섞은 lobe 하나가 된다 (spec :3532, :3838-3851).
    // metal은 single scattering이라 rough할수록 energy를 잃는다. spec은 multiple scattering을 권하지만 (:3637) 뒤로 미뤘다.
    input.specularF0 = lerp(kGltfDielectricF0, baseColor, metallic);

    // dielectric의 diffuse와 specular는 Kulla & Conty의 energy-preserving coupling
    // f_d = ρ (1 − E_s(μ_o)) (1 − E_s(μ_i)) / (π (1 − E_avg))로 잇는다. sample implementation의 fresnel_mix는 energy conservation을 깨고
    // (spec :3877), 정답으로 쓰는 BRDF는 energy conserving이어야 한다 (MUST, :3651). (1 − F(μ_i))(1 − F(μ_o)) (:3879)도 energy를
    // 지키지만 grazing에서 어두워져, ρ = 1인 dielectric이 모든 빛을 반사하지 않는다.
    const float viewAlbedo = GetDielectricSpecularAlbedo(input.viewCosine, input.alpha);
    input.diffuseFactor = (1.0 - metallic) * baseColor * (1.0 - viewAlbedo) * kInversePi / (1.0 - GetAverageDielectricSpecularAlbedo(input.alpha));

    // lobe를 각 lobe의 directional albedo에 비례해 고른다. diffuse는 ρ (1 − E_s(μ_o))로 정확하고, specular는 dielectric의 E_s를 f0의
    // Fresnel 비로 늘인 어림이다.
    const float specularAlbedo = viewAlbedo * dot(kRec709Luminance, GetSchlickFresnel(input.specularF0, input.viewCosine)) /
        GetSchlickFresnel(kGltfDielectricF0, input.viewCosine).x;
    const float diffuseAlbedo = dot(kRec709Luminance, (1.0 - metallic) * baseColor) * (1.0 - viewAlbedo);
    input.specularProbability = specularAlbedo + diffuseAlbedo > 0.0 ? specularAlbedo / (specularAlbedo + diffuseAlbedo) : 1.0;
    return input;
}

// light 쪽에서 와서 view 쪽으로 반사하는 빛의 f. light는 단위 vector이고, normal 아래에서 오는 light는 0이다.
// distribution은 그 방향의 D다. 다음 방향의 pdf가 같은 값을 쓴다.
float3 EvaluateGltfBrdf(GltfBrdfInput input, float3 light, out float distribution)
{
    distribution = 0.0;
    const float lightCosine = dot(input.normal, light);
    if (lightCosine <= 0.0)
        return 0.0;
    const float3 halfway = input.view + light;
    const float viewHalfCosine = saturate(dot(input.view, halfway) * rsqrt(dot(halfway, halfway)));
    distribution = GetMicrofacetDistribution(input.normal, halfway, input.alpha);
    const float3 specular = GetSchlickFresnel(input.specularF0, viewHalfCosine) *
        (distribution * GetVisibility(input.viewCosine, lightCosine, input.alpha));
    return specular + input.diffuseFactor * (1.0 - GetDielectricSpecularAlbedo(lightCosine, input.alpha));
}

float3 EvaluateGltfBrdf(GltfBrdfInput input, float3 light)
{
    float distribution;
    return EvaluateGltfBrdf(input, light, distribution);
}

// Sampling Visible GGX Normals with Spherical Caps - Jonathan Dupuy and Anis Benyoub 2023
// https://doi.org/10.1111/cgf.14867
// localView는 normal이 +z인 공간의 view이고 z ≥ 0이다. samples는 [0, 1)의 두 수다.
float3 SampleVisibleMicrofacetNormal(float roughnessAlpha, float3 localView, float2 samples)
{
    // Stretch the view vector as if roughness==1
    float3 wiStd = normalize(float3(roughnessAlpha * localView.xy, localView.z));

    float phi = 6.28318530717958647692f * samples.y;
    float z = mad(-wiStd.z, samples.x, 1.0f - samples.x);
    float sinTheta = sqrt(saturate(1.0f - z * z));
    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);
    float3 c = float3(x, y, z);
    float3 wmStd = c + wiStd;

    // Convert normal to un-stretched and normalise
    float3 wm = normalize(float3(roughnessAlpha * wmStd.xy, wmStd.z));
    return wm;
}

struct GltfBrdfSample
{
    // 단위 vector
    float3 direction;
    // f cos / pdf. direction이 normal 아래면 0이다.
    float3 weight;
};

// 다음 방향을 specularProbability로 고른 lobe에서 고른다. specular는 visible normal을 골라 view를 반사하고, diffuse는 cosine-weighted
// hemisphere다. weight의 pdf는 두 lobe의 pdf를 그 확률로 섞은 것이라, 어느 lobe에서 골랐든 같은 식이다. samples는 [0, 1)의 세 수다.
GltfBrdfSample SampleGltfBrdfDirection(GltfBrdfInput input, float3 samples)
{
    GltfBrdfSample sample;
    if (samples.x < input.specularProbability)
    {
        float3 tangent, bitangent;
        GetOrthoVectors(input.normal, tangent, bitangent);
        const float3 localView = float3(dot(input.view, tangent), dot(input.view, bitangent), input.viewCosine);
        const float3 localLight = reflect(-localView, SampleVisibleMicrofacetNormal(input.alpha, localView, samples.yz));
        sample.direction = normalize(tangent * localLight.x + bitangent * localLight.y + input.normal * localLight.z);
    }
    else
    {
        sample.direction = SampleLambertDirection(input.normal, samples.yz);
    }

    const float lightCosine = dot(input.normal, sample.direction);
    float distribution;
    const float3 f = EvaluateGltfBrdf(input, sample.direction, distribution);
    // visible normal을 고르고 반사한 방향의 pdf는 G1(v) D / (4 μ_o) = D / (2 (μ_o + sqrt(α² + (1 − α²) μ_o²)))다.
    const float alphaSquared = input.alpha * input.alpha;
    const float specularPdf = distribution /
        (2.0 * (input.viewCosine + sqrt(alphaSquared + (1.0 - alphaSquared) * input.viewCosine * input.viewCosine)));
    const float pdf = input.specularProbability * specularPdf + (1.0 - input.specularProbability) * lightCosine * kInversePi;
    sample.weight = lightCosine > 0.0 ? f * (lightCosine / pdf) : 0.0;
    return sample;
}

#endif
