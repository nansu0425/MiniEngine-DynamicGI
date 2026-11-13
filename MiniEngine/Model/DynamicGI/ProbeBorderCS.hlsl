#include "Core/Shaders/IrradianceProbes.hlsli"

cbuffer Constants : register(b0)
{
    IrradianceProbeGrid Grid;
};

RWTexture2D<float4> ProbesAtlas : register(u0);

uint2 GetBorderSource(uint2 border)
{
    static const uint kLast = kProbeTileSize - 1;
    const bool2 isEdge = or(border == 0, border == kLast);
    if (all(isEdge))
        return select(border == 0, uint2(kProbeTexelsPerSide, kProbeTexelsPerSide), uint2(1, 1));
    if (isEdge.y)
        return uint2(kLast - border.x, border.y == 0 ? 1 : kProbeTexelsPerSide);
    return uint2(border.x == 0 ? 1 : kProbeTexelsPerSide, kLast - border.y);
}

[numthreads(kProbeTileSize, kProbeTileSize, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    static const uint kLast = kProbeTileSize - 1;
    if (all(and(GTid.xy != 0, GTid.xy != kLast)))
        return;

    const uint2 tileOrigin = GetProbeTileOrigin(Grid, GetProbeCoordinate(Grid, Gid.x));
    ProbesAtlas[tileOrigin + GTid.xy] = ProbesAtlas[tileOrigin + GetBorderSource(GTid.xy)];
}
