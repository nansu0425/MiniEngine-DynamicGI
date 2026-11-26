#include "Core/pch.h"
#include "Core/Profiling/GpuMemory.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Resource/GpuResource.h"

namespace
{
    std::wstring GetDebugName(ID3D12Resource& resource)
    {
        UINT size = 0;
        if (FAILED(resource.GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, nullptr)) || size < sizeof(wchar_t))
            return std::wstring();

        std::wstring name(size / sizeof(wchar_t), L'\0');
        if (FAILED(resource.GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, name.data())))
            return std::wstring();
        name.resize(wcsnlen(name.c_str(), name.size()));
        return name;
    }
}

GpuMemory::Allocation GpuMemory::DescribeAllocation(const GpuResource& resource)
{
    ID3D12Resource* d3dResource = const_cast<ID3D12Resource*>(resource.GetResource());
    if (d3dResource == nullptr)
        return Allocation();

    Allocation allocation = { .name = GetDebugName(*d3dResource), .desc = d3dResource->GetDesc() };
    allocation.bytes = Graphics::g_Device->GetResourceAllocationInfo(0, 1, &allocation.desc).SizeInBytes;
    return allocation;
}
