#pragma once

#include "Model/DynamicGI/ProbeRelocation.h"
#include "Model/Renderer/IrradianceProbeGrid.h"

#include <optional>
#include <vector>

namespace ProbeReadback
{
    struct Census
    {
        uint32_t insideGeometry;
        uint32_t relocationResults[(uint32_t)ProbeRelocation::Result::kCount];
    };

    struct CensusReadback
    {
        uint32_t id;
        uint32_t updates;
        uint32_t resets;
        uint32_t probes;
        float maxBackFaceRatio;
        Census census;
    };

    struct ProbeRelocationReadback
    {
        DirectX::XMFLOAT3 offset;
        ProbeRelocation::Result result;
    };

    struct ProbeRay
    {
        DirectX::XMFLOAT3 direction;
        float weight;
        DirectX::XMFLOAT3 radiance;
        std::optional<float> hitDistance;
    };

    struct ProbeRaysReadback
    {
        uint32_t probe;
        uint32_t updates;
        uint32_t resets;
        uint32_t uniformRaysPerProbe;
        std::vector<ProbeRay> rays;
    };

    void Initialize(void);
    void Shutdown(void);

    void Update(std::optional<uint32_t> probeOfWatchedRays);

    Math::Vector3 GetProbePosition(const IrradianceProbeGrid& grid, uint32_t probe);
    std::optional<ProbeRelocationReadback> FindProbeRelocation(const IrradianceProbeGrid& grid, uint32_t probe);

    uint32_t RequestCensus(float maxBackFaceRatio);
    const std::optional<CensusReadback>& GetCensus(void);

    const ProbeRaysReadback* FindRaysOfLastUpdate(uint32_t probe);
    const ProbeRaysReadback* FindLastReadRays(uint32_t probe);
}
