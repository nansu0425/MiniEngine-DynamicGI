#include "Model/DynamicGI/ProbeDistanceLine.hlsli"
#include "Model/DynamicGI/ProbeGuide.hlsli"
#include "Model/DynamicGI/ProbePositions.hlsli"

Texture2D<float2> ProbesDistance : register(t0);
Texture2D<float4> RaySamples : register(t1);
Texture2D<float> ProbesTexelDistance : register(t2);
Texture2D<float4> ProbesOffset : register(t3);
Texture2D<float4> WeightedRayDirections : register(t4);

static const float3 kTexelMeanColor = float3(1.0, 0.85, 0.0);
static const float3 kTexelDistanceColor = float3(1.0, 0.0, 1.0);
static const float3 kRaySampleColor = float3(0.0, 0.8, 1.0);
static const float3 kRaySampleAtMaxDistanceColor = float3(1.0, 0.0, 0.0);
static const float3 kUniformRaySampleColor = float3(0.0, 0.8, 1.0);
static const float3 kGuidedRaySampleColor = float3(1.0, 0.55, 0.0);
static const float3 kSmallestFluxRaySampleColor = float3(0.1, 0.25, 0.7);
static const float3 kLargestFluxRaySampleColor = float3(1.0, 0.9, 0.2);
static const float3 kHighlightedRaySampleColor = float3(1.0, 1.0, 1.0);
static const float kOpacityOfSmallestFluxRaySample = 0.05;
static const float kOpacityBesideHighlightedRay = 0.2;

struct LineStyle
{
    float3 color;
    float opacity;
};

float GetRayFlux(uint ray)
{
    const uint2 raySample = uint2(ray, Probe);
    return GetProbeRayFlux(WeightedRayDirections[raySample].w, RaySamples[raySample].rgb);
}

float GetRayFluxPerLargestFlux(uint ray)
{
    float largestFlux = 0.0;
    for (uint other = 0; other < RaysPerProbe; ++other)
        largestFlux = max(largestFlux, GetRayFlux(other));
    return largestFlux > 0.0 ? GetRayFlux(ray) / largestFlux : 0.0;
}

LineStyle GetRaySampleStyle(uint ray, float hitDistance)
{
    LineStyle style;
    style.opacity = 1.0;
    if (RaySampleColor == kRaySampleColorOfKind)
        style.color = ray < UniformRaysPerProbe ? kUniformRaySampleColor : kGuidedRaySampleColor;
    else if (RaySampleColor == kRaySampleColorOfContribution)
    {
        const float fluxPerLargestFlux = GetRayFluxPerLargestFlux(ray);
        style.color = lerp(kSmallestFluxRaySampleColor, kLargestFluxRaySampleColor, fluxPerLargestFlux);
        style.opacity = lerp(kOpacityOfSmallestFluxRaySample, 1.0, fluxPerLargestFlux);
    }
    else
        style.color = hitDistance >= GetProbeRayMaxDistance(Grid) ? kRaySampleAtMaxDistanceColor : kRaySampleColor;
    return style;
}

bool HasHighlightedRay()
{
    return HighlightedRay < RaysPerProbe;
}

LineStyle PickOutHighlightedRay(LineStyle style, uint lineIndex)
{
    if (!HasHighlightedRay())
        return style;

    LineStyle pickedOut = style;
    const bool isRaySample = Lines == kProbeDistanceLinesOfRaySamples;
    if (isRaySample && lineIndex == HighlightedRay)
    {
        pickedOut.color = kHighlightedRaySampleColor;
        pickedOut.opacity = 1.0;
    }
    else
        pickedOut.opacity *= kOpacityBesideHighlightedRay;
    return pickedOut;
}

ProbeDistanceLineVertex main(uint vertexID : SV_VertexID, uint lineIndex : SV_InstanceID)
{
    const uint3 coordinate = GetProbeCoordinate(Grid, Probe);
    float3 direction;
    float distance;
    LineStyle style;
    style.opacity = 1.0;
    if (Lines != kProbeDistanceLinesOfRaySamples)
    {
        const uint2 probeTexel = uint2(lineIndex % DistanceTexelsPerSide, lineIndex / DistanceTexelsPerSide);
        const uint2 atlasTexel = GetProbeAtlasTexel(Grid, DistanceTexelsPerSide, coordinate, probeTexel);
        direction = GetProbeTexelDirection(DistanceTexelsPerSide, probeTexel);
        const bool isMean = Lines == kProbeDistanceLinesOfTexelMeans;
        distance = isMean ? ProbesDistance[atlasTexel].x : ProbesTexelDistance[atlasTexel];
        style.color = isMean ? kTexelMeanColor : kTexelDistanceColor;
    }
    else
    {
        const float hitDistance = RaySamples[uint2(lineIndex, Probe)].a;
        direction = WeightedRayDirections[uint2(lineIndex, Probe)].xyz;
        distance = HitsAnything(hitDistance) ? hitDistance : GetProbeRayMaxDistance(Grid);
        style = GetRaySampleStyle(lineIndex, hitDistance);
    }
    style = PickOutHighlightedRay(style, lineIndex);

    ProbeDistanceLineVertex vertex;
    vertex.color = style.color;
    vertex.opacity = style.opacity;
    const float3 position = GetProbePosition(MakeProbePositions(Grid, ProbesOffset), coordinate) + direction * distance * vertexID;
    vertex.position = mul(ViewProj, float4(position, 1.0));
    return vertex;
}
