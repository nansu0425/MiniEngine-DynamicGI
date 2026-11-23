#include "Model/DynamicGI/ProbeRays.hlsli"

cbuffer Constants : register(b0)
{
    float BlendWeight;
    ProbeRays Rays;
};

Texture2D<float> RayBackFaces : register(t0);
RWTexture2D<float> ProbesBackFaceRatio : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint probe = DTid.x;
    if (probe >= GetProbeCount(Rays.grid))
        return;

    float backFaces = 0.0;
    for (uint ray = 0; ray < Rays.uniformRaysPerProbe; ++ray)
        backFaces += RayBackFaces[uint2(ray, probe)];
    const float ratio = backFaces / Rays.uniformRaysPerProbe;

    const uint2 texel = uint2(probe, 0);
    ProbesBackFaceRatio[texel] = lerp(ProbesBackFaceRatio[texel], ratio, BlendWeight);
}
