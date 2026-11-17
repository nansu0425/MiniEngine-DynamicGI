#include "Model/DynamicGI/ProbeClassification.hlsli"
#include "Model/DynamicGI/ProbeRelocation.hlsli"

cbuffer Constants : register(b0)
{
    float MaxBackFaceRatio;
    IrradianceProbeGrid Grid;
};

Texture2D<float> ProbesBackFaceRatio : register(t0);
Texture2D<float4> ProbesOffset : register(t1);
RWByteAddressBuffer Census : register(u0);

static const uint kInsideGeometryAddress = 0;
static const uint kFirstRelocationResultAddress = 4;

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint probe = DTid.x;
    if (probe >= GetProbeCount(Grid))
        return;

    const uint3 coordinate = GetProbeCoordinate(Grid, probe);
    if (IsProbeInsideGeometry(MaxBackFaceRatio, GetProbeBackFaceRatio(ProbesBackFaceRatio, Grid, coordinate)))
        Census.InterlockedAdd(kInsideGeometryAddress, 1);
    const uint result = GetProbeRelocationResult(MakeProbePositions(Grid, ProbesOffset), coordinate);
    Census.InterlockedAdd(kFirstRelocationResultAddress + 4 * result, 1);
}
