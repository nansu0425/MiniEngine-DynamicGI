#include "Model/DynamicGI/ProbeRays.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    float DistanceSharpness;
    ProbeRays Rays;
};

Texture2D<float4> RaySamples : register(t0);
RWTexture2D<float4> ProbesIrradiance : register(u0);
RWTexture2D<float2> ProbesDistance : register(u1);

[numthreads(kProbeTexelsPerSide, kProbeTexelsPerSide, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const uint probe = Gid.x;
    const float3 texelDirection = GetProbeTexelDirection(GTid.xy);

    float3 weightedRadiance = 0.0;
    float radianceWeightSum = 0.0;
    float2 weightedMoments = 0.0;
    float momentWeightSum = 0.0;
    for (uint ray = 0; ray < Rays.raysPerProbe; ++ray)
    {
        const float radianceWeight = max(0.0, dot(texelDirection, GetProbeRayDirection(Rays, ray)));
        const float momentWeight = pow(radianceWeight, DistanceSharpness);
        const float4 sample = RaySamples[uint2(ray, probe)];
        weightedRadiance += radianceWeight * sample.rgb;
        radianceWeightSum += radianceWeight;
        weightedMoments += momentWeight * float2(sample.a, sample.a * sample.a);
        momentWeightSum += momentWeight;
    }
    const float3 irradiance = radianceWeightSum > 0.0 ? weightedRadiance / radianceWeightSum : 0.0;
    const float2 moments = momentWeightSum > 0.0 ? weightedMoments / momentWeightSum : 0.0;

    const uint2 texel = GetProbeAtlasTexel(Rays.grid, GetProbeCoordinate(Rays.grid, probe), GTid.xy);
    ProbesIrradiance[texel] = float4(lerp(ProbesIrradiance[texel].rgb, irradiance, BlendWeight), 0.0);
    ProbesDistance[texel] = lerp(ProbesDistance[texel], moments, BlendWeight);
}
