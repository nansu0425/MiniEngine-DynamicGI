#pragma once

#include "Core/pch.h"

namespace ProbeWeighting
{
    enum class Visibility { kNone, kShadowRay, kChebyshev };

    struct Settings
    {
        Visibility visibility = Visibility::kNone;
    };

    __declspec(align(16)) struct Constants
    {
        uint32_t visibility;
    };

    inline Constants DescribeConstants(const Settings& settings)
    {
        return { (uint32_t)settings.visibility };
    }

    inline bool InterpolatesTrilinearly(const Settings& settings)
    {
        return settings.visibility == Visibility::kNone;
    }
}
