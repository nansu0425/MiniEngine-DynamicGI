#pragma once

#include "Core/pch.h"

namespace ProbeWeighting
{
    enum class Visibility { kNone, kShadowRay, kChebyshev };
    enum class FacingNormal { kGeometry, kShading };

    struct Settings
    {
        Visibility visibility = Visibility::kNone;
        float selfShadowBias = 0.0f;
        float chebyshevExponent = 1.0f;
        bool facing = true;
        float minFacingWeight = 0.2f;
        FacingNormal facingNormal = FacingNormal::kShading;
        bool classification = true;
        float maxBackFaceRatio = 0.25f;
    };

    __declspec(align(16)) struct Constants
    {
        uint32_t visibility;
        float selfShadowBias;
        float chebyshevExponent;
        uint32_t facing;
        float minFacingWeight;
        uint32_t facingNormal;
        uint32_t classification;
        float maxBackFaceRatio;
    };

    inline Constants DescribeConstants(const Settings& settings)
    {
        return {
            .visibility = (uint32_t)settings.visibility,
            .selfShadowBias = settings.selfShadowBias,
            .chebyshevExponent = settings.chebyshevExponent,
            .facing = settings.facing ? 1u : 0u,
            .minFacingWeight = settings.minFacingWeight,
            .facingNormal = (uint32_t)settings.facingNormal,
            .classification = settings.classification ? 1u : 0u,
            .maxBackFaceRatio = settings.maxBackFaceRatio,
        };
    }

    inline bool InterpolatesTrilinearly(const Settings& settings)
    {
        return settings.visibility == Visibility::kNone && !settings.facing && !settings.classification;
    }
}
