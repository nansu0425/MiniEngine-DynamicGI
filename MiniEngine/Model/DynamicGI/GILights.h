#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"

__declspec(align(16)) struct GILights
{
    Math::Vector3 lightDirection;
    Math::Vector3 lightIlluminance;
    Math::Vector3 skyRadiance;
    Math::Matrix4 shadowMatrix;
};

inline GILights DescribeGILights(const GIFrameInputs& inputs)
{
    return { inputs.lightDirection, inputs.lightIlluminance, inputs.skyRadiance, inputs.shadowMatrix };
}
