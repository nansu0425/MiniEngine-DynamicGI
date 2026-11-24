#pragma once

#include "Core/pch.h"

__declspec(align(16)) struct SceneLights
{
    // surface에서 directional light를 향하는 방향과, light에 수직인 면의 illuminance (lux)
    Math::Vector3 lightDirection;
    Math::Vector3 lightIlluminance;
    // sky의 radiance (cd/m²). 모든 방향에서 같다.
    Math::Vector3 skyRadiance;
};
