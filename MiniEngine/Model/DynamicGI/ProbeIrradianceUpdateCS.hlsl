#include "Model/DynamicGI/ProbeRays.hlsli"
#include "Model/DynamicGI/ProbeEmissiveSamples.hlsli"
#include "Model/DynamicGI/ProbePositions.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/ShaderUtility.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    float SpikeLimitSigmas;
    float SpikeLimitFloor;
    ProbeEmissiveSampling EmissiveSampling;
    ProbeRays Rays;
};

Texture2D<float4> RaySamples : register(t0);
Texture2D<float4> EmissiveSamples : register(t1);
StructuredBuffer<EmissiveTriangle> EmissiveTriangles : register(t2);
Texture2D<float4> ProbesOffset : register(t3);
Texture2D<float4> WeightedRayDirections : register(t4);
RWTexture2D<float4> ProbesIrradiance : register(u0);

float3 EstimateIrradianceFromRaySamples(uint probe, float3 texelDirection)
{
    float3 weightedRadiance = 0.0;
    float weightSum = 0.0;
    for (uint ray = 0; ray < GetProbeRayCount(Rays); ++ray)
    {
        const float4 weightedDirection = WeightedRayDirections[uint2(ray, probe)];
        const float weight = weightedDirection.w * max(0.0, dot(texelDirection, weightedDirection.xyz));
        weightedRadiance += weight * RaySamples[uint2(ray, probe)].rgb;
        weightSum += weight;
    }
    return weightSum > 0.0 ? weightedRadiance / weightSum : 0.0;
}

float3 EstimateIrradianceFromEmissiveSamples(uint probe, float3 texelDirection)
{
    if (!HasEmissiveSamples(EmissiveSampling))
        return 0.0;

    const ProbePositions positions = MakeProbePositions(Rays.grid, ProbesOffset);
    const float3 origin = GetProbePosition(positions, GetProbeCoordinate(Rays.grid, probe));
    float3 weightedIrradiance = 0.0;
    for (uint sample = 0; sample < EmissiveSampling.samplesPerProbe; ++sample)
    {
        const EmissivePoint emissivePoint = SampleProbeEmissivePoint(EmissiveSampling, EmissiveTriangles, probe, sample);
        const float weight = max(0.0, dot(texelDirection, normalize(emissivePoint.position - origin)));
        weightedIrradiance += weight * EmissiveSamples[uint2(sample, probe)].rgb;
    }
    return weightedIrradiance / (kPi * EmissiveSampling.samplesPerProbe);
}

struct StoredIrradiance
{
    float3 irradiance;
    float squaredLuminance;
};

StoredIrradiance GetStoredIrradiance(uint2 texel)
{
    const float4 stored = ProbesIrradiance[texel];
    StoredIrradiance storedIrradiance;
    storedIrradiance.irradiance = stored.rgb;
    storedIrradiance.squaredLuminance = stored.a;
    return storedIrradiance;
}

float3 LimitSpike(float3 irradiance, StoredIrradiance stored)
{
    if (SpikeLimitSigmas <= 0.0)
        return irradiance;

    const float meanLuminance = RGBToLuminance(stored.irradiance);
    const float luminanceDeviation = sqrt(max(0.0, stored.squaredLuminance - meanLuminance * meanLuminance));
    const float largestLuminance = max(meanLuminance + SpikeLimitSigmas * luminanceDeviation, SpikeLimitFloor);
    const float luminance = RGBToLuminance(irradiance);
    return luminance > largestLuminance ? irradiance * (largestLuminance / luminance) : irradiance;
}

[numthreads(kProbeIrradianceTexelsPerSide, kProbeIrradianceTexelsPerSide, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const uint probe = Gid.x;
    const float3 texelDirection = GetProbeTexelDirection(kProbeIrradianceTexelsPerSide, GTid.xy);
    const float3 irradiance = EstimateIrradianceFromRaySamples(probe, texelDirection) + EstimateIrradianceFromEmissiveSamples(probe, texelDirection);

    const uint2 texel = GetProbeAtlasTexel(Rays.grid, kProbeIrradianceTexelsPerSide, GetProbeCoordinate(Rays.grid, probe), GTid.xy);
    const StoredIrradiance stored = GetStoredIrradiance(texel);
    const float3 limitedIrradiance = LimitSpike(irradiance, stored);
    const float limitedLuminance = RGBToLuminance(limitedIrradiance);
    const float3 blendedIrradiance = lerp(stored.irradiance, limitedIrradiance, BlendWeight);
    const float blendedSquaredLuminance = lerp(stored.squaredLuminance, limitedLuminance * limitedLuminance, BlendWeight);
    ProbesIrradiance[texel] = float4(blendedIrradiance, blendedSquaredLuminance);
}
