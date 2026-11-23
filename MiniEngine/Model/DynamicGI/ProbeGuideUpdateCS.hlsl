#include "Model/DynamicGI/ProbeGuide.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    ProbeRays Rays;
};

Texture2D<float4> RaySamples : register(t0);
Texture2D<float4> WeightedRayDirections : register(t4);
RWTexture2D<float2> ProbesGuide : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint probe = DTid.x;
    if (probe >= GetProbeCount(Rays.grid))
        return;

    float flux[kProbeGuideTexelCount];
    uint texel;
    for (texel = 0; texel < kProbeGuideTexelCount; ++texel)
        flux[texel] = 0.0;
    for (uint ray = 0; ray < GetProbeRayCount(Rays); ++ray)
    {
        const float4 weightedDirection = WeightedRayDirections[uint2(ray, probe)];
        // 한 texel의 flux = 해당 texel 범위에 해당하는 모든 ray에 대한 sum(ray가 담당하는 넓이 (solid angle) * ray의 밝기 (luminance))
        flux[GetProbeGuideTexelIndex(weightedDirection.xyz)] += GetProbeRayFlux(weightedDirection.w, RaySamples[uint2(ray, probe)].rgb);
    }

    float cumulativeFlux = 0.0;
    for (texel = 0; texel < kProbeGuideTexelCount; ++texel)
    {
        const float blendedFlux = lerp(ProbesGuide[uint2(texel, probe)].x, flux[texel], BlendWeight);
        cumulativeFlux += blendedFlux;
        ProbesGuide[uint2(texel, probe)] = float2(blendedFlux, cumulativeFlux);
    }
}
