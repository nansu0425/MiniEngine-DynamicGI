#include "Model/DynamicGI/ProbeOffsetLine.hlsli"
#include "Model/DynamicGI/ProbeRelocation.hlsli"

Texture2D<float4> ProbesOffset : register(t0);

static const float3 kNotMovedColor = float3(0.8, 0.8, 0.8);
static const float3 kBackFacePushColor = float3(0.1, 0.85, 0.6);
static const float3 kBlockedColor = float3(1.0, 0.35, 0.2);

float3 GetRelocationColor(uint result)
{
    if (result == kProbeRelocationBackFacePush)
        return kBackFacePushColor;
    return result == kProbeRelocationBlocked ? kBlockedColor : kNotMovedColor;
}

ProbeOffsetLineVertex main(uint vertexID : SV_VertexID, uint probe : SV_InstanceID)
{
    const ProbePositions positions = MakeProbePositions(Grid, ProbesOffset);
    const uint3 coordinate = GetProbeCoordinate(Grid, probe);
    const float3 position = vertexID == 0 ? GetProbeGridPosition(Grid, coordinate) : GetProbePosition(positions, coordinate);

    ProbeOffsetLineVertex vertex;
    vertex.position = mul(ViewProj, float4(position, 1.0));
    vertex.color = GetRelocationColor(GetProbeRelocationResult(positions, coordinate));
    return vertex;
}
