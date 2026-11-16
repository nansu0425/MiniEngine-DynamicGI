#include "Model/DynamicGI/DynamicGIVolumeBox.hlsli"

static const uint kFaceCorners[36] =
{
    0, 2, 6, 0, 6, 4,
    1, 5, 7, 1, 7, 3,
    0, 4, 5, 0, 5, 1,
    2, 3, 7, 2, 7, 6,
    0, 1, 3, 0, 3, 2,
    4, 6, 7, 4, 7, 5,
};

float4 main(uint vertexID : SV_VertexID) : SV_Position
{
    return ProjectBoxCorner(kFaceCorners[vertexID]);
}
