#include "Model/DynamicGI/ProbeRays.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    ProbeRays Rays;
};

Texture2D<float4> RaySamples : register(t0);
RWTexture2D<float4> ProbesIrradiance : register(u0);
RWTexture2D<float2> ProbesDistance : register(u1);

[numthreads(kProbeTexelsPerSide, kProbeTexelsPerSide, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const uint probe = Gid.x;
    const float3 texelDirection = DecodeOctahedral((float2(GTid.xy) + 0.5) / kProbeTexelsPerSide);

    float3 weightedRadiance = 0.0;
    float2 weightedMoments = 0.0;
    float weightSum = 0.0;
    for (uint ray = 0; ray < Rays.raysPerProbe; ++ray)
    {
        const float weight = max(0.0, dot(texelDirection, GetProbeRayDirection(Rays, ray)));
        const float4 sample = RaySamples[uint2(ray, probe)];
        weightedRadiance += weight * sample.rgb;
        weightedMoments += weight * float2(sample.a, sample.a * sample.a);
        weightSum += weight;
    }
    const float3 irradiance = weightSum > 0.0 ? weightedRadiance / weightSum : 0.0;
    const float2 moments = weightSum > 0.0 ? weightedMoments / weightSum : 0.0;

    const uint2 texel = GetProbeTileOrigin(Rays.grid, GetProbeCoordinate(Rays.grid, probe)) + 1 + GTid.xy;
    ProbesIrradiance[texel] = float4(lerp(ProbesIrradiance[texel].rgb, irradiance, BlendWeight), 0.0);
    ProbesDistance[texel] = lerp(ProbesDistance[texel], moments, BlendWeight);
}
