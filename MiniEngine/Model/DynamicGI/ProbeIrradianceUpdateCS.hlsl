#include "Model/DynamicGI/ProbeRays.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    ProbeRays Rays;
};

Texture2D<float4> RaySamples : register(t0);
RWTexture2D<float4> ProbesIrradiance : register(u0);

[numthreads(kProbeIrradianceTexelsPerSide, kProbeIrradianceTexelsPerSide, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const uint probe = Gid.x;
    const float3 texelDirection = GetProbeTexelDirection(kProbeIrradianceTexelsPerSide, GTid.xy);

    float3 weightedRadiance = 0.0;
    float weightSum = 0.0;
    for (uint ray = 0; ray < Rays.raysPerProbe; ++ray)
    {
        const float weight = max(0.0, dot(texelDirection, GetProbeRayDirection(Rays, ray)));
        weightedRadiance += weight * RaySamples[uint2(ray, probe)].rgb;
        weightSum += weight;
    }
    const float3 irradiance = weightSum > 0.0 ? weightedRadiance / weightSum : 0.0;

    const uint2 texel = GetProbeAtlasTexel(Rays.grid, kProbeIrradianceTexelsPerSide, GetProbeCoordinate(Rays.grid, probe), GTid.xy);
    ProbesIrradiance[texel] = float4(lerp(ProbesIrradiance[texel].rgb, irradiance, BlendWeight), 0.0);
}
