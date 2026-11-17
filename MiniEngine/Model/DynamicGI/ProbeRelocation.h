#pragma once

#include "Core/pch.h"

namespace ProbeRelocation
{
    struct Settings
    {
        bool enabled = true;
        uint32_t iterations = 5;
        float maxOffset = 0.5f;
        float maxPushScale = 1.99f;
        float minPushScale = 1.0f;

        bool operator==(const Settings&) const = default;
    };

    enum class Result : uint32_t { kNotMoved, kBackFacePush, kBlocked, kCount };

    inline const char* GetResultName(Result result)
    {
        const char* names[] = { "Not Moved", "Back Face Push", "Blocked" };
        return names[(uint32_t)result];
    }
}
