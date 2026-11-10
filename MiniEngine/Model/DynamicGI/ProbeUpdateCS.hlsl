#include "Model/DynamicGI/ProbeRays.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    ProbeRays Rays;
};

Texture2D<float4> RayRadiance : register(t0);
RWTexture2D<float4> ProbeIrradiance : register(u0);

[numthreads(kProbeTexelsPerSide, kProbeTexelsPerSide, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const uint probe = Gid.x;
    const float3 texelDirection = DecodeOctahedral((float2(GTid.xy) + 0.5) / kProbeTexelsPerSide);

    float3 weightedRadiance = 0.0;
    float weightSum = 0.0;
    for (uint ray = 0; ray < Rays.raysPerProbe; ++ray)
    {
        const float weight = max(0.0, dot(texelDirection, GetProbeRayDirection(Rays, ray)));
        weightedRadiance += weight * RayRadiance[uint2(ray, probe)].rgb;
        weightSum += weight;
    }
    const float3 irradiance = weightSum > 0.0 ? weightedRadiance / weightSum : 0.0;

    const uint2 texel = GetProbeTileOrigin(Rays.grid, GetProbeCoordinate(Rays.grid, probe)) + 1 + GTid.xy;
    ProbeIrradiance[texel] = float4(lerp(ProbeIrradiance[texel].rgb, irradiance, BlendWeight), 0.0);
}
