#include "Model/DynamicGI/ProbeGuideLine.hlsli"
#include "Model/DynamicGI/ProbeGuide.hlsli"
#include "Model/DynamicGI/ProbePositions.hlsli"

Texture2D<float4> ProbesOffset : register(t0);

static const uint kNoHighlightedTexel = 0xffffffff;
static const float3 kGuideTexelColor = float3(0.2, 1.0, 0.4);
static const float3 kHighlightedGuideTexelColor = float3(1.0, 1.0, 1.0);
static const float kOpacityBesideHighlightedTexel = 0.2;

ProbeGuideLineVertex main(uint vertexID : SV_VertexID, uint texel : SV_InstanceID)
{
    const uint3 coordinate = GetProbeCoordinate(Grid, Probe);
    const float3 direction = GetProbeTexelDirection(kProbeGuideTexelsPerSide, GetProbeGuideTexelCoordinate(texel));

    const bool highlighted = texel == HighlightedTexel;
    ProbeGuideLineVertex vertex;
    vertex.color = highlighted ? kHighlightedGuideTexelColor : kGuideTexelColor;
    vertex.opacity = HighlightedTexel != kNoHighlightedTexel && !highlighted ? kOpacityBesideHighlightedTexel : 1.0;
    const float3 position = GetProbePosition(MakeProbePositions(Grid, ProbesOffset), coordinate) + direction * Length * vertexID;
    vertex.position = mul(ViewProj, float4(position, 1.0));
    return vertex;
}
