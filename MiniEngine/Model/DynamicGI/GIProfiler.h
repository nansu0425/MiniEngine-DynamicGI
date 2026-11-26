#pragma once

#include "Model/DynamicGI/GIProfiling.h"

#include <functional>

namespace ControlChannel
{
    class Reply;
}

namespace GIProfiler
{
    void Initialize(const std::function<GIProfiling::Report(void)>& describeReport);
    void Shutdown(void);

    void Update(void);

    void Measure(uint32_t frames, const ControlChannel::Reply& reply);
}
