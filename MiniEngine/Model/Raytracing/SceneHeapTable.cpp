#include "Model/Raytracing/SceneHeapTable.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/GpuResource.h"

void SceneHeapTable::Create(uint32_t numDescriptors)
{
    m_FirstDescriptor = Renderer::s_TextureHeap.Alloc(numDescriptors);
    m_Slots.assign(numDescriptors, Slot());
}

void SceneHeapTable::PointTo(uint32_t slot, const GpuResource& resource, D3D12_CPU_DESCRIPTOR_HANDLE view)
{
    ASSERT(slot < m_Slots.size());

    Slot& current = m_Slots[slot];
    if (current.resource == &resource && current.versionID == resource.GetVersionID())
        return;

    const DescriptorHandle descriptor = m_FirstDescriptor + slot * Renderer::s_TextureHeap.GetDescriptorSize();
    Graphics::g_Device->CopyDescriptorsSimple(1, descriptor, view, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    current.resource = &resource;
    current.versionID = resource.GetVersionID();
}

void SceneHeapTable::Bind(ComputeContext& context, UINT rootIndex) const
{
    context.SetDescriptorTable(rootIndex, m_FirstDescriptor);
}
