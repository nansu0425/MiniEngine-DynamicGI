#pragma once

#include "Model/DynamicGI/GIFrameInputs.h"

__declspec(align(16)) struct GILights
{
    SceneLights sceneLights;
    Math::Matrix4 shadowMatrix;
};

inline GILights DescribeGILights(const GIFrameInputs& inputs)
{
    return { inputs.lights, inputs.shadowMatrix };
}
