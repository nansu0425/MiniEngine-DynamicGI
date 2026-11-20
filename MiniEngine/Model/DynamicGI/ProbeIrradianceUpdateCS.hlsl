#include "Model/DynamicGI/ProbeRays.hlsli"
#include "Model/DynamicGI/ProbeEmissiveSamples.hlsli"
#include "Model/DynamicGI/ProbePositions.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    ProbeEmissiveSampling EmissiveSampling;
    ProbeRays Rays;
};

Texture2D<float4> RaySamples : register(t0);
Texture2D<float4> EmissiveSamples : register(t1);
StructuredBuffer<EmissiveTriangle> EmissiveTriangles : register(t2);
Texture2D<float4> ProbesOffset : register(t3);
RWTexture2D<float4> ProbesIrradiance : register(u0);

float3 EstimateIrradianceFromRaySamples(uint probe, float3 texelDirection)
{
    float3 weightedRadiance = 0.0;
    float weightSum = 0.0;
    for (uint ray = 0; ray < Rays.raysPerProbe; ++ray)
    {
        const float weight = max(0.0, dot(texelDirection, GetProbeRayDirection(Rays, ray)));
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

[numthreads(kProbeIrradianceTexelsPerSide, kProbeIrradianceTexelsPerSide, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const uint probe = Gid.x;
    const float3 texelDirection = GetProbeTexelDirection(kProbeIrradianceTexelsPerSide, GTid.xy);
    const float3 irradiance = EstimateIrradianceFromRaySamples(probe, texelDirection) + EstimateIrradianceFromEmissiveSamples(probe, texelDirection);

    const uint2 texel = GetProbeAtlasTexel(Rays.grid, kProbeIrradianceTexelsPerSide, GetProbeCoordinate(Rays.grid, probe), GTid.xy);
    ProbesIrradiance[texel] = float4(lerp(ProbesIrradiance[texel].rgb, irradiance, BlendWeight), 0.0);
}
