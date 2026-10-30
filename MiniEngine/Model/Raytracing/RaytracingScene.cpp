#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Model.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Descriptor/DescriptorHeap.h"
#include "Core/Graphics/Pipeline/RootSignature.h"

using namespace Math;
using namespace Graphics;

namespace
{
    enum RootParameter { kScene, kBLASInstances, kBuffers, kTextures, kSamplers, kNumRootParameters };
    static_assert(kNumRootParameters == RaytracingScene::kNumRootParameters, "root parameter 개수가 RaytracingScene.h와 다르다");
}

void RaytracingScene::InitRootParameters(RootSignature& rootSignature, UINT firstRootIndex)
{
    // register와 space는 RaytracingScene.hlsli의 선언과 같다.
    rootSignature[firstRootIndex + kScene].InitAsBufferSRV(0, D3D12_SHADER_VISIBILITY_ALL, 1);
    rootSignature[firstRootIndex + kBLASInstances].InitAsBufferSRV(1, D3D12_SHADER_VISIBILITY_ALL, 1);
    rootSignature[firstRootIndex + kBuffers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, UINT_MAX, D3D12_SHADER_VISIBILITY_ALL, 2);
    rootSignature[firstRootIndex + kTextures].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, UINT_MAX, D3D12_SHADER_VISIBILITY_ALL, 3);
    rootSignature[firstRootIndex + kSamplers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, UINT_MAX, D3D12_SHADER_VISIBILITY_ALL, 1);
}

void RaytracingScene::AddModelInstance(const ModelInstance& modelInstance)
{
    if (modelInstance.IsNull())
        return;

    const Model& model = modelInstance.GetModel();
    const AffineTransform* nodeWorldTransforms = modelInstance.GetNodeWorldTransforms();
    const uint8_t* pMesh = model.m_MeshData.get();

    for (uint32_t i = 0; i < model.m_NumMeshes; ++i)
    {
        const Mesh& mesh = *(const Mesh*)pMesh;
        pMesh += sizeof(Mesh) + (mesh.numDraws - 1) * sizeof(Mesh::Draw);

        if (model.m_MeshBLAS[i] == D3D12_GPU_VIRTUAL_ADDRESS_NULL)
            continue;

        D3D12_RAYTRACING_INSTANCE_DESC desc = {};
        XMStoreFloat3x4((XMFLOAT3X4*)desc.Transform, nodeWorldTransforms[mesh.meshCBV]);
        desc.InstanceMask = 0xFF;
        // doubleSided material은 뒷면도 그리므로 그 mesh의 BLAS instance는 ray의 back face culling을 받지 않는다.
        // mesh 하나가 material 하나를 쓰므로 instance flag로 material마다 정할 수 있다.
        if (mesh.psoFlags & PSOFlags::kTwoSided)
            desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        desc.AccelerationStructure = model.m_MeshBLAS[i];
        m_InstanceDescs.push_back(desc);

        m_BLASInstances.push_back({ model.m_RaytracingSRVs, model.m_MeshFirstBLASGeometry[i] });
    }
}

void RaytracingScene::Build(CommandContext& context)
{
    const uint32_t numInstances = (uint32_t)m_InstanceDescs.size();
    if (numInstances == 0)
        return;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = build.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = numInstances;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    // 앞 frame이 아직 쓰고 있을 수 있는 buffer를 바꾸지 않도록 Destroy 뒤 처음 한 번만 만든다. Destroy하는 쪽이 GPU를 기다린다.
    if (m_MaxInstances == 0)
        CreateBuffers(inputs);
    ASSERT(numInstances <= m_MaxInstances);

    // 앞 frame이 이 buffer를 읽는 command는 앞선 ExecuteCommandLists에 있으므로 copy 전에 끝난다.
    context.WriteBuffer(m_BLASInstanceBuffer, 0, m_BLASInstances.data(), numInstances * sizeof(BLASInstance));
    context.TransitionResource(m_BLASInstanceBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_BLASInstances.clear();

    ScopedTimer _prof(L"Build TLAS", context);

    const size_t instanceDescsSize = numInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    DynAlloc instanceDescs = context.ReserveUploadMemory(instanceDescsSize);
    std::memcpy(instanceDescs.DataPtr, m_InstanceDescs.data(), instanceDescsSize);
    m_InstanceDescs.clear();

    inputs.InstanceDescs = instanceDescs.GpuAddress;
    build.DestAccelerationStructureData = m_TLAS.GetGpuVirtualAddress();
    build.ScratchAccelerationStructureData = m_ScratchBuffer.GetGpuVirtualAddress();

    // 앞의 TLAS 읽기나 scratch 쓰기, 뒤의 TLAS 읽기가 build와 같은 ExecuteCommandLists 안에 있어도
    // 순서가 지켜지게 한다. 서로 다른 ExecuteCommandLists 사이는 D3D12가 이미 완료를 보장한다.
    context.TransitionResource(m_ScratchBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.InsertUAVBarrier(m_TLAS);
    context.BuildRaytracingAccelerationStructure(build);
    context.InsertUAVBarrier(m_TLAS);
}

void RaytracingScene::Destroy()
{
    m_InstanceDescs.clear();
    m_BLASInstances.clear();
    m_TLAS.Destroy();
    m_ScratchBuffer.Destroy();
    m_BLASInstanceBuffer.Destroy();
    m_MaxInstances = 0;
}

void RaytracingScene::Bind(ComputeContext& context, UINT firstRootIndex) const
{
    context.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());
    context.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, Renderer::s_SamplerHeap.GetHeapPointer());

    context.SetAccelerationStructure(firstRootIndex + kScene, m_TLAS);
    context.SetBufferSRV(firstRootIndex + kBLASInstances, m_BLASInstanceBuffer);
    context.SetDescriptorTable(firstRootIndex + kBuffers, Renderer::s_TextureHeap[0]);
    context.SetDescriptorTable(firstRootIndex + kTextures, Renderer::s_TextureHeap[0]);
    context.SetDescriptorTable(firstRootIndex + kSamplers, Renderer::s_SamplerHeap[0]);
}

void RaytracingScene::CreateBuffers(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild;
    g_DXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

    m_TLAS.Create(L"TLAS", prebuild.ResultDataMaxSizeInBytes);
    m_ScratchBuffer.Create(L"TLAS Scratch", (uint32_t)prebuild.ScratchDataSizeInBytes, 1);
    m_BLASInstanceBuffer.Create(L"BLAS Instances", inputs.NumDescs, sizeof(BLASInstance));
    m_MaxInstances = inputs.NumDescs;
}
