#include "Core/pch.h"
#include "Core/Graphics/Resource/AccelerationStructureBuffer.h"
#include "Core/Graphics/GraphicsCore.h"

using namespace Graphics;

void AccelerationStructureBuffer::Create(const std::wstring& name, uint64_t sizeInBytes)
{
    Destroy();

    const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_UsageState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

    ASSERT_SUCCEEDED(g_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, m_UsageState, nullptr, MY_IID_PPV_ARGS(&m_pResource)));

    m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

#ifdef RELEASE
    (name);
#else
    m_pResource->SetName(name.c_str());
#endif
}
