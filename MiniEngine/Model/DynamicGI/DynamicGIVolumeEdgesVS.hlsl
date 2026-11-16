#include "Model/DynamicGI/DynamicGIVolumeBox.hlsli"

static const uint kEdgeCorners[24] =
{
    0, 1, 2, 3, 4, 5, 6, 7,
    0, 2, 1, 3, 4, 6, 5, 7,
    0, 4, 1, 5, 2, 6, 3, 7,
};

float4 main(uint vertexID : SV_VertexID) : SV_Position
{
    return ProjectBoxCorner(kEdgeCorners[vertexID]);
}
