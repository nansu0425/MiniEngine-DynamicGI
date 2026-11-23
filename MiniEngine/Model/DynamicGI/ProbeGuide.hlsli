#ifndef __PROBE_GUIDE_HLSLI__
#define __PROBE_GUIDE_HLSLI__

#include "Core/Shaders/Random.hlsli"
#include "Model/DynamicGI/ProbeRays.hlsli"

static const uint kProbeGuideTexelsPerSide = 16;
static const uint kProbeGuideTexelCount = kProbeGuideTexelsPerSide * kProbeGuideTexelsPerSide;
static const uint kGuidedRayStratumRandomSequence = 1;
static const uint kGuidedRayJitterRandomSequence = 2;

struct ProbeGuideTexel
{
    float flux;
    float cumulativeFlux;
};

ProbeGuideTexel GetProbeGuideTexel(Texture2D<float2> guide, uint probe, uint texel)
{
    const float2 stored = guide[uint2(texel, probe)];
    ProbeGuideTexel guideTexel;
    guideTexel.flux = stored.x;
    guideTexel.cumulativeFlux = stored.y;
    return guideTexel;
}

float GetProbeGuideTotalFlux(Texture2D<float2> guide, uint probe)
{
    return GetProbeGuideTexel(guide, probe, kProbeGuideTexelCount - 1).cumulativeFlux;
}

uint GetProbeGuideTexelIndex(float3 direction)
{
    const uint2 coordinate = min(uint2(EncodeOctahedral(direction) * kProbeGuideTexelsPerSide), kProbeGuideTexelsPerSide - 1);
    return coordinate.x + coordinate.y * kProbeGuideTexelsPerSide;
}

float GetProbeGuideTexelProbability(Texture2D<float2> guide, uint probe, uint texel)
{
    const float totalFlux = GetProbeGuideTotalFlux(guide, probe);
    if (totalFlux <= 0.0)
        return 1.0 / kProbeGuideTexelCount;
    return GetProbeGuideTexel(guide, probe, texel).flux / totalFlux;
}

uint SampleProbeGuideTexelIndex(Texture2D<float2> guide, uint probe, float cumulativeProbability)
{
    const float totalFlux = GetProbeGuideTotalFlux(guide, probe);
    if (totalFlux <= 0.0)
        return min(uint(cumulativeProbability * kProbeGuideTexelCount), kProbeGuideTexelCount - 1);

    const float cumulativeFlux = cumulativeProbability * totalFlux;
    uint first = 0;
    uint last = kProbeGuideTexelCount - 1;
    while (first < last)
    {
        const uint middle = (first + last) / 2;
        if (GetProbeGuideTexel(guide, probe, middle).cumulativeFlux > cumulativeFlux)
            last = middle;
        else
            first = middle + 1;
    }
    return first;
}

float GetProbeGuideProbabilityDensity(Texture2D<float2> guide, uint probe, float3 direction)
{
    const float manhattanLength = abs(direction.x) + abs(direction.y) + abs(direction.z);
    const float solidAnglePerOctahedralArea = 4.0 * manhattanLength * manhattanLength * manhattanLength;
    const float probabilityPerOctahedralArea = GetProbeGuideTexelProbability(guide, probe, GetProbeGuideTexelIndex(direction)) * kProbeGuideTexelCount;
    return probabilityPerOctahedralArea / solidAnglePerOctahedralArea;
}

float3 SampleProbeGuideDirection(Texture2D<float2> guide, ProbeRays rays, uint probe, uint guidedRay)
{
    // guide texel 고르기
    Random stratumRandom = MakeRandom(rays.update, uint2(0, probe), kGuidedRayStratumRandomSequence);
    const float cumulativeProbability = (guidedRay + stratumRandom.rand()) / rays.guidedRaysPerProbe;
    const uint texel = SampleProbeGuideTexelIndex(guide, probe, cumulativeProbability);

    // texel 내의 점을 고르기
    Random jitterRandom = MakeRandom(rays.update, uint2(guidedRay, probe), kGuidedRayJitterRandomSequence);
    const float2 texelCoordinate = float2(texel % kProbeGuideTexelsPerSide, texel / kProbeGuideTexelsPerSide);
    return DecodeOctahedral((texelCoordinate + jitterRandom.rand2()) / kProbeGuideTexelsPerSide);
}

#endif
