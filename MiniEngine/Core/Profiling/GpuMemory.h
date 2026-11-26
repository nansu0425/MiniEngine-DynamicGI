#pragma once

#include "Core/pch.h"

class GpuResource;

namespace GpuMemory
{
    struct Allocation
    {
        std::wstring name;
        D3D12_RESOURCE_DESC desc = {};
        uint64_t bytes = 0;
    };

    Allocation DescribeAllocation(const GpuResource& resource);
}
