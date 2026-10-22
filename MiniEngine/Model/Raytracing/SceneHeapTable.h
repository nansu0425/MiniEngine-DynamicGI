#pragma once

#include "Core/pch.h"
#include "Core/Graphics/Descriptor/DescriptorHeap.h"

class ComputeContext;
class GpuResource;

// ray pass가 자기 resource를 가리키는 descriptor table. RaytracingScene::Bind가 bind하는 scene descriptor heap 안에 둔다.
// PointTo는 heap의 descriptor를 바로 덮어쓴다. 그래서 slot이 가리키는 resource를 바꾸거나 그 resource를 다시 만드는 것은
// 그 descriptor를 읽는 GPU 작업이 남아 있지 않을 때만 해야 한다.
class SceneHeapTable
{
public:
    void Create(uint32_t numDescriptors);

    // table의 slot이 resource의 view를 가리키게 한다. view는 slot이 다른 resource를 가리키던 때나 resource가 다시 만들어졌을 때만 복사한다.
    void PointTo(uint32_t slot, const GpuResource& resource, D3D12_CPU_DESCRIPTOR_HANDLE view);

    // RaytracingScene::Bind가 scene descriptor heap을 bind한 뒤, 다른 heap이 bind되기 전에 부른다.
    void Bind(ComputeContext& context, UINT rootIndex) const;

private:
    // slot이 지금 가리키는 resource와, view를 복사했을 때 그 resource의 version ID
    struct Slot
    {
        const GpuResource* resource = nullptr;
        uint32_t versionID = 0;
    };

    DescriptorHandle m_FirstDescriptor;
    std::vector<Slot> m_Slots;
};
