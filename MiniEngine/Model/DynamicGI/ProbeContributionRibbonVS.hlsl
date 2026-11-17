#include "Model/DynamicGI/ProbeContributionRibbon.hlsli"

StructuredBuffer<SurfaceProbeContribution> Contribution : register(t0);

static const float3 kCornerColor = float3(1.0, 0.55, 0.1);
static const float3 kCellEdgeColor = float3(0.6, 0.6, 0.6);
static const float3 kNormalColor = float3(0.3, 0.75, 1.0);
static const float3 kDistanceMeanColor = float3(1.0, 0.2, 1.0);
static const float3 kDistanceLookupColor = float3(0.3, 1.0, 0.4);
static const float kUnhoveredCornerBrightness = 0.25;
static const float kOccludedCornerPixels = 1.5;
static const float kMinCornerPixels = 2.0;
static const float kMaxCornerPixels = 12.0;
static const float kCellEdgePixels = 1.0;
static const float kNormalPixels = 2.0;
static const float kDistanceMeanPixels = 2.0;
static const float kDistanceLookupPixels = 2.0;
static const float kArrowHeadLengthPixels = 12.0;
static const float kArrowHeadWidthPixels = 10.0;
static const float kDistanceMeanGapPixels = 3.0;
static const float kPointerWidthPixels = 8.0;
static const uint kNumShaftVertices = 6;

static const uint kNoHead = 0;
static const uint kArrowHead = 1;
static const uint kPointerHead = 2;

struct Ribbon
{
    float3 start;
    float3 end;
    float pixels;
    float sidePixels;
    float3 color;
    bool dashed;
    uint head;
    bool hidden;
};

float GetCornerPixels(float weight)
{
    return weight == 0.0 ? kOccludedCornerPixels : lerp(kMinCornerPixels, kMaxCornerPixels, weight);
}

float3 GetCornerGridPosition(SurfaceProbeContribution contribution, uint corner)
{
    return GetProbeGridPosition(Grid, contribution.corners[corner].coordinate);
}

uint2 GetCellEdgeCorners(uint edge)
{
    const uint axisBit = 1u << (edge / 4);
    const uint otherBits = edge % 4;
    const uint lowBits = otherBits & (axisBit - 1);
    const uint corner = lowBits | ((otherBits - lowBits) << 1);
    return uint2(corner, corner | axisBit);
}

Ribbon DescribeRibbon(SurfaceProbeContribution contribution, uint index)
{
    Ribbon ribbon;
    ribbon.sidePixels = 0.0;
    ribbon.dashed = false;
    ribbon.head = kNoHead;
    ribbon.hidden = false;
    if (index < kFirstCellEdgeRibbon)
    {
        const float weight = contribution.corners[index].weight;
        ribbon.start = contribution.distanceLookupPosition;
        ribbon.end = contribution.corners[index].position;
        ribbon.dashed = weight == 0.0;
        ribbon.pixels = GetCornerPixels(weight);
        ribbon.color = kCornerColor;
        if (HoveredCorner != kNoHoveredCorner && HoveredCorner != index)
            ribbon.color *= kUnhoveredCornerBrightness;
    }
    else if (index < kNormalRibbon)
    {
        const uint2 corners = GetCellEdgeCorners(index - kFirstCellEdgeRibbon);
        ribbon.start = GetCornerGridPosition(contribution, corners.x);
        ribbon.end = GetCornerGridPosition(contribution, corners.y);
        ribbon.pixels = kCellEdgePixels;
        ribbon.color = kCellEdgeColor;
    }
    else if (index < kFirstDistanceMeanRibbon)
    {
        ribbon.start = contribution.position;
        ribbon.end = contribution.position + contribution.shadingNormal * NormalLength;
        ribbon.pixels = kNormalPixels;
        ribbon.color = kNormalColor;
        ribbon.head = kArrowHead;
    }
    else if (index < kDistanceLookupRibbon)
    {
        const uint corner = index - kFirstDistanceMeanRibbon;
        const ProbeCornerContribution probe = contribution.corners[corner];
        ribbon.start = probe.position;
        ribbon.end = ribbon.start + normalize(contribution.distanceLookupPosition - ribbon.start) * probe.distanceMean;
        ribbon.pixels = kDistanceMeanPixels;
        ribbon.sidePixels = 0.5 * GetCornerPixels(probe.weight) + kDistanceMeanGapPixels + 0.5 * kDistanceMeanPixels;
        ribbon.color = kDistanceMeanColor;
        ribbon.head = kPointerHead;
        ribbon.hidden = contribution.comparesDistance == 0 || (HoveredCorner != kNoHoveredCorner && HoveredCorner != corner);
    }
    else
    {
        ribbon.start = contribution.position;
        ribbon.end = contribution.distanceLookupPosition;
        ribbon.pixels = kDistanceLookupPixels;
        ribbon.color = kDistanceLookupColor;
        ribbon.head = kArrowHead;
        ribbon.hidden = all(ribbon.start == ribbon.end);
    }
    return ribbon;
}

bool ClipToNearPlane(inout float4 start, inout float4 end)
{
    if (start.w < NearClip && end.w < NearClip)
        return false;
    if (start.w < NearClip)
        start = lerp(start, end, (NearClip - start.w) / (end.w - start.w));
    else if (end.w < NearClip)
        end = lerp(end, start, (NearClip - end.w) / (start.w - end.w));
    return true;
}

float4 OffsetByPixels(float4 position, float2 pixels)
{
    position.xy += pixels / (0.5 * ViewportSize) * position.w;
    return position;
}

ProbeContributionRibbonVertex main(uint vertexID : SV_VertexID, uint index : SV_InstanceID)
{
    const Ribbon ribbon = DescribeRibbon(Contribution[0], index);
    ProbeContributionRibbonVertex vertex;
    vertex.color = ribbon.color;
    vertex.dashed = ribbon.dashed;
    vertex.pixelsAlong = 0.0;
    vertex.position = float4(0.0, 0.0, -1.0, 1.0);

    float4 start = mul(ViewProj, float4(ribbon.start, 1.0));
    float4 end = mul(ViewProj, float4(ribbon.end, 1.0));
    if (ribbon.hidden || !ClipToNearPlane(start, end) || (vertexID >= kNumShaftVertices && ribbon.head == kNoHead))
        return vertex;

    const float2 startPixel = start.xy / start.w * 0.5 * ViewportSize;
    const float2 endPixel = end.xy / end.w * 0.5 * ViewportSize;
    const float pixelLength = length(endPixel - startPixel);
    const float2 along = pixelLength > 0.0 ? (endPixel - startPixel) / pixelLength : float2(1.0, 0.0);
    const float2 across = float2(-along.y, along.x);
    const float headLength = ribbon.head == kArrowHead ? min(kArrowHeadLengthPixels, pixelLength) : 0.0;

    if (vertexID >= kNumShaftVertices)
    {
        const float headSide[3] = { 0.0, -1.0, 1.0 };
        const uint headVertex = vertexID - kNumShaftVertices;
        if (headVertex == 0)
            vertex.position = end;
        else if (ribbon.head == kArrowHead)
            vertex.position = OffsetByPixels(end, -along * headLength + across * headSide[headVertex] * kArrowHeadWidthPixels * 0.5);
        else
            vertex.position = OffsetByPixels(end, across * ribbon.sidePixels + along * headSide[headVertex] * kPointerWidthPixels * 0.5);
        return vertex;
    }

    const uint shaftAtEnd[6] = { 0, 1, 0, 0, 1, 1 };
    const float shaftSide[6] = { -1.0, -1.0, 1.0, 1.0, -1.0, 1.0 };
    const bool atEnd = shaftAtEnd[vertexID] != 0;
    const float4 shaftEnd = OffsetByPixels(end, -along * headLength);
    vertex.position = OffsetByPixels(atEnd ? shaftEnd : start, across * (ribbon.sidePixels + shaftSide[vertexID] * ribbon.pixels * 0.5));
    vertex.pixelsAlong = atEnd ? pixelLength - headLength : 0.0;
    return vertex;
}
