#include "Model/DynamicGI/ProbeGuideLine.hlsli"

float4 main(ProbeGuideLineVertex vertex) : SV_Target0
{
    return float4(vertex.color * vertex.opacity, vertex.opacity);
}
