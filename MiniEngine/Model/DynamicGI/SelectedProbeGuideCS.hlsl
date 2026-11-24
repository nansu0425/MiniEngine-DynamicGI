#include "Model/DynamicGI/ProbeGuide.hlsli"

cbuffer Constants : register(b0)
{
    uint Probe;
    uint HasProbe;
};

Texture2D<float2> ProbesGuide : register(t0);
RWTexture2D<float2> SelectedProbeGuide : register(u0);

[numthreads(kProbeGuideTexelsPerSide, kProbeGuideTexelsPerSide, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    SelectedProbeGuide[DTid.xy] = HasProbe != 0 ? ProbesGuide[uint2(GetProbeGuideTexelIndexAt(DTid.xy), Probe)] : float2(0.0, 0.0);
}
