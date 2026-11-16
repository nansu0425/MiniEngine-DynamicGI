#include "Model/DynamicGI/DynamicGIVolumeBox.hlsli"

float4 main() : SV_Target0
{
    return float4(Color * Opacity, Opacity);
}
