#include "Core/Shaders/IrradianceProbes.hlsli"

cbuffer Constants : register(b0)
{
    uint TexelsPerSide;
    IrradianceProbeGrid Grid;
};

RWTexture2D<float4> ProbesAtlas : register(u0);

uint2 GetBorderSource(uint2 border, uint last)
{
    const bool2 isEdge = or(border == 0, border == last);
    if (all(isEdge))
        return select(border == 0, uint2(TexelsPerSide, TexelsPerSide), uint2(1, 1));
    if (isEdge.y)
        return uint2(last - border.x, border.y == 0 ? 1 : TexelsPerSide);
    return uint2(border.x == 0 ? 1 : TexelsPerSide, last - border.y);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 tileTexel = DTid.xy;
    const uint last = GetProbeTileSize(TexelsPerSide) - 1;
    if (any(tileTexel > last) || all(and(tileTexel != 0, tileTexel != last)))
        return;

    const uint2 tileOrigin = GetProbeTileOrigin(Grid, TexelsPerSide, GetProbeCoordinate(Grid, DTid.z));
    ProbesAtlas[tileOrigin + tileTexel] = ProbesAtlas[tileOrigin + GetBorderSource(tileTexel, last)];
}
