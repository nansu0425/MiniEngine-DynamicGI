#include "Model/DynamicGI/ProbeOffsetLine.hlsli"

float4 main(ProbeOffsetLineVertex vertex) : SV_Target0
{
    return float4(vertex.color, 1.0);
}
