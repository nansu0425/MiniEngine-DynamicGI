#ifndef __IRRADIANCE_PROBES_HLSLI__
#define __IRRADIANCE_PROBES_HLSLI__

static const uint kProbeTexelsPerSide = 8;
static const uint kProbeTileSize = kProbeTexelsPerSide + 2;

struct IrradianceProbeGrid
{
    float3 origin;
    float3 spacing;
    uint3 counts;
};

float2 EncodeOctahedral(float3 direction)
{
    const float3 onOctahedron = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    float2 folded = onOctahedron.xy;
    if (onOctahedron.z < 0.0)
        folded = (1.0 - abs(onOctahedron.yx)) * select(onOctahedron.xy >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
    return folded * 0.5 + 0.5;
}

float3 DecodeOctahedral(float2 coordinate)
{
    const float2 folded = coordinate * 2.0 - 1.0;
    float3 direction = float3(folded, 1.0 - abs(folded.x) - abs(folded.y));
    if (direction.z < 0.0)
        direction.xy = (1.0 - abs(direction.yx)) * select(direction.xy >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
    return normalize(direction);
}

uint GetProbeCount(IrradianceProbeGrid grid)
{
    return grid.counts.x * grid.counts.y * grid.counts.z;
}

uint3 GetProbeCoordinate(IrradianceProbeGrid grid, uint probe)
{
    return uint3(probe % grid.counts.x, (probe / grid.counts.x) % grid.counts.y, probe / (grid.counts.x * grid.counts.y));
}

float3 GetProbePosition(IrradianceProbeGrid grid, uint3 coordinate)
{
    return grid.origin + float3(coordinate) * grid.spacing;
}

uint2 GetProbeTileOrigin(IrradianceProbeGrid grid, uint3 coordinate)
{
    return uint2(coordinate.x + coordinate.y * grid.counts.x, coordinate.z) * kProbeTileSize;
}

float2 GetProbeAtlasSize(IrradianceProbeGrid grid)
{
    return float2(grid.counts.x * grid.counts.y, grid.counts.z) * kProbeTileSize;
}

float2 GetProbeAtlasUV(IrradianceProbeGrid grid, uint3 coordinate, float3 direction)
{
    const float2 texel = float2(GetProbeTileOrigin(grid, coordinate)) + 1.0 + EncodeOctahedral(direction) * kProbeTexelsPerSide;
    return texel / GetProbeAtlasSize(grid);
}

float3 SampleProbeIrradiance(Texture2D<float4> atlas, SamplerState linearClamp, IrradianceProbeGrid grid, uint3 coordinate, float3 direction)
{
    return atlas.SampleLevel(linearClamp, GetProbeAtlasUV(grid, coordinate, direction), 0.0).rgb;
}

static const uint kNumProbeCorners = 8;

struct IrradianceProbeCorner
{
    uint3 coordinate;
    float trilinearWeight;
};

IrradianceProbeCorner GetIrradianceProbeCorner(IrradianceProbeGrid grid, float3 position, uint corner)
{
    const float3 local = (position - grid.origin) / grid.spacing;
    const int3 baseCoordinate = clamp(int3(floor(local)), 0, int3(grid.counts) - 2);
    const float3 alpha = saturate(local - float3(baseCoordinate));
    const uint3 offset = uint3(corner & 1, (corner >> 1) & 1, corner >> 2);
    const float3 axisWeights = select(offset != 0, alpha, 1.0 - alpha);

    IrradianceProbeCorner probe;
    probe.coordinate = uint3(baseCoordinate) + offset;
    probe.trilinearWeight = axisWeights.x * axisWeights.y * axisWeights.z;
    return probe;
}

float3 SampleIrradianceProbes(Texture2D<float4> atlas, SamplerState linearClamp, IrradianceProbeGrid grid, float3 position, float3 direction)
{
    float3 irradiance = 0.0;
    [unroll]
    for (uint corner = 0; corner < kNumProbeCorners; ++corner)
    {
        const IrradianceProbeCorner probe = GetIrradianceProbeCorner(grid, position, corner);
        irradiance += probe.trilinearWeight * SampleProbeIrradiance(atlas, linearClamp, grid, probe.coordinate, direction);
    }
    return irradiance;
}

#endif
