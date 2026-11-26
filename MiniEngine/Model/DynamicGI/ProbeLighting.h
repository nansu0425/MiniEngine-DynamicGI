#pragma once

#include "Core/Math/BoundingBox.h"
#include "Model/DynamicGI/GIFrameInputs.h"
#include "Model/DynamicGI/GIProfiling.h"
#include "Model/DynamicGI/ProbeRelocation.h"
#include "Model/DynamicGI/ProbeWeighting.h"
#include "Model/Renderer/IrradianceProbeGrid.h"

class ColorBuffer;
class ComputeContext;

namespace ProbeLighting
{
    struct SpikeLimit
    {
        bool enabled = true;
        float sigmas = 4.0f;
        float floor = 0.01f;
    };

    struct Settings
    {
        uint32_t probeCounts[3] = { 16, 8, 8 };
        uint32_t uniformRaysPerProbe = 64;
        uint32_t guidedRaysPerProbe = 0;
        uint32_t emissiveSamplesPerProbe = 0;
        float hysteresis = 0.95f;
        SpikeLimit spikeLimit;
        bool randomRayRotation = true;
        float distanceSharpness = 1.0f;
        uint32_t distanceTexelsPerSide = 8;
        float backFaceDistanceScale = 0.2f;
        ProbeWeighting::Settings weighting;
        ProbeRelocation::Settings relocation;
    };

    struct Volume
    {
        Math::AxisAlignedBox bounds;
        Settings settings;
    };

    struct Probes
    {
        const IrradianceProbeGrid& grid;
        ColorBuffer& irradiance;
        ColorBuffer& distance;
        ColorBuffer& texelDistance;
        ColorBuffer& backFaceRatio;
        ColorBuffer& offset;
        uint32_t distanceTexelsPerSide;
        ColorBuffer& guide;
        ColorBuffer& weightedRayDirections;
        ColorBuffer& raySamples;
        uint32_t raysPerProbe;
        uint32_t uniformRaysPerProbe;
        ColorBuffer& emissiveSamples;
    };

    struct Progress
    {
        uint32_t updates;
        uint32_t resets;
    };

    void Initialize(void);
    void Shutdown(void);

    void Reset(void);
    Progress GetProgress(void);

    void Update(ComputeContext& context, const Volume& volume, const GIFrameInputs& inputs);
    void EstimateScreenIrradiance(ComputeContext& context, const ProbeWeighting::Settings& weighting, const GIFrameInputs& inputs, uint32_t width, uint32_t height);

    Probes GetProbes(void);
    ColorBuffer& GetScreenIrradiance(void);

    void ListResources(std::vector<GIProfiling::Resource>& resources);
    void DescribeWorkload(GIProfiling::Workload& workload);
}
