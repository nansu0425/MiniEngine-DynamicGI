#include "Model/DynamicGI/ProbeContributionRibbon.hlsli"

static const float kDashPixels = 6.0;

float4 main(ProbeContributionRibbonVertex vertex) : SV_Target0
{
    if (vertex.dashed != 0 && frac(vertex.pixelsAlong / (2.0 * kDashPixels)) >= 0.5)
        discard;
    return float4(vertex.color, 1.0);
}
