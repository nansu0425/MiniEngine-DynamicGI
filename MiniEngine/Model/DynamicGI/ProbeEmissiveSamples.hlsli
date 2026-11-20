#ifndef __PROBE_EMISSIVE_SAMPLES_HLSLI__
#define __PROBE_EMISSIVE_SAMPLES_HLSLI__

#include "Core/Shaders/Random.hlsli"
#include "Model/Raytracing/EmissiveTriangles.hlsli"

struct ProbeEmissiveSampling
{
    uint samplesPerProbe;
    uint emissiveTriangleCount;
    uint update;
};

bool HasEmissiveSamples(ProbeEmissiveSampling sampling)
{
    return sampling.samplesPerProbe > 0 && sampling.emissiveTriangleCount > 0;
}

EmissivePoint SampleProbeEmissivePoint(ProbeEmissiveSampling sampling, StructuredBuffer<EmissiveTriangle> triangles, uint probe, uint sample)
{
    Random random = MakeRandom(sampling.update, uint2(sample, probe), 0);
    const float3 u = float3(random.rand(), random.rand(), random.rand());
    return SampleEmissivePoint(triangles, sampling.emissiveTriangleCount, u);
}

#endif
