#pragma once

#include "Core/pch.h"

namespace ProbeWeighting
{
    enum class Visibility { kNone, kShadowRay, kChebyshev };
    enum class FacingNormal { kGeometry, kShading };

    struct Settings
    {
        Visibility visibility = Visibility::kNone;
        bool facing = true;
        float minFacingWeight = 0.2f;
        FacingNormal facingNormal = FacingNormal::kShading;
    };

    __declspec(align(16)) struct Constants
    {
        uint32_t visibility;
        uint32_t facing;
        float minFacingWeight;
        uint32_t facingNormal;
    };

    inline Constants DescribeConstants(const Settings& settings)
    {
        return { (uint32_t)settings.visibility, settings.facing ? 1u : 0u, settings.minFacingWeight, (uint32_t)settings.facingNormal };
    }

    inline bool InterpolatesTrilinearly(const Settings& settings)
    {
        return settings.visibility == Visibility::kNone && !settings.facing;
    }
}
