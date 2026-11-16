#pragma once

#include "Model/DynamicGI/ProbeLighting.h"

#include <optional>

class RaytracingScene;
class StructuredBuffer;

namespace ProbeContribution
{
    constexpr uint32_t kNumCorners = 8;

    struct Corner
    {
        DirectX::XMUINT3 coordinate;
        float trilinearWeight;
        float backFaceRatio;
        float activeWeight;
        float visibilityWeight;
        float facingWeight;
        float weight;
        DirectX::XMFLOAT3 irradiance;
        float distance;
        float distanceMean;
    };

    struct Surface
    {
        uint32_t hasSurface;
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 distanceLookupPosition;
        DirectX::XMFLOAT3 geometryNormal;
        DirectX::XMFLOAT3 shadingNormal;
        uint32_t comparesDistance;
        uint32_t fallsBackToTrilinear;
        DirectX::XMFLOAT3 irradiance;
        DirectX::XMFLOAT3 shadingIrradiance;
        Corner corners[kNumCorners];
    };

    struct Readback
    {
        uint32_t captureId;
        Surface surface;
    };

    inline char GetCornerLetter(uint32_t corner)
    {
        return (char)('A' + corner);
    }

    void Initialize(void);
    void Shutdown(void);

    uint32_t CaptureSurface(DirectX::XMUINT2 scenePixel);
    void Measure(const ProbeWeighting::Settings& weighting, const RaytracingScene& scene);

    const std::optional<Readback>& GetReadback(void);
    StructuredBuffer& GetSurfaceBuffer(void);
}
