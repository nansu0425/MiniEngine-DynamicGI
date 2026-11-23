#include "Model/DynamicGI/ProbeRays.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    float DistanceSharpness;
    uint DistanceTexelsPerSide;
    float BackFaceDistanceScale;
    ProbeRays Rays;
};

Texture2D<float4> RaySamples : register(t0);
Texture2D<float> RayBackFaces : register(t1);
RWTexture2D<float2> ProbesDistance : register(u0);

float GetRayKeptDistance(uint2 raySample)
{
    const float distance = GetProbeRayKeptDistance(Rays.grid, RaySamples[raySample].a);
    return RayBackFaces[raySample] > 0.0 ? distance * BackFaceDistanceScale : distance;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 probeTexel = DTid.xy;
    if (any(probeTexel >= DistanceTexelsPerSide))
        return;

    const uint probe = DTid.z;
    const float3 texelDirection = GetProbeTexelDirection(DistanceTexelsPerSide, probeTexel);

    float2 weightedMoments = 0.0;
    float weightSum = 0.0;
    for (uint ray = 0; ray < Rays.uniformRaysPerProbe; ++ray)
    {
        const float weight = pow(max(0.0, dot(texelDirection, GetUniformProbeRayDirection(Rays, ray))), DistanceSharpness);
        const float distance = GetRayKeptDistance(uint2(ray, probe));
        weightedMoments += weight * float2(distance, distance * distance);
        weightSum += weight;
    }
    const float2 moments = weightSum > 0.0 ? weightedMoments / weightSum : 0.0;

    const uint2 texel = GetProbeAtlasTexel(Rays.grid, DistanceTexelsPerSide, GetProbeCoordinate(Rays.grid, probe), probeTexel);
    ProbesDistance[texel] = lerp(ProbesDistance[texel], moments, BlendWeight);
}
