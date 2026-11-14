#include "Model/DynamicGI/ProbeDistanceLine.hlsli"

float4 main(ProbeDistanceLineVertex vertex) : SV_Target0
{
    return float4(vertex.color, 1.0);
}
