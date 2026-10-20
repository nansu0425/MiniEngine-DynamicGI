#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Model.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"

using namespace Math;
using namespace Graphics;

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
        desc.AccelerationStructure = model.m_MeshBLAS[i];
        m_Instances.push_back(desc);
    }
}

void RaytracingScene::Build(CommandContext& context)
{
    const uint32_t numInstances = (uint32_t)m_Instances.size();
    if (numInstances == 0)
        return;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = build.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = numInstances;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    // 앞 frame이 아직 쓰고 있을 수 있는 buffer를 바꾸지 않도록 처음 한 번만 만든다.
    if (m_MaxInstances == 0)
        CreateBuffers(inputs);
    ASSERT(numInstances <= m_MaxInstances);

    ScopedTimer _prof(L"Build TLAS", context);

    const size_t instanceDescsSize = numInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    DynAlloc instanceDescs = context.ReserveUploadMemory(instanceDescsSize);
    std::memcpy(instanceDescs.DataPtr, m_Instances.data(), instanceDescsSize);
    m_Instances.clear();

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
    m_Instances.clear();
    m_TLAS.Destroy();
    m_ScratchBuffer.Destroy();
    m_MaxInstances = 0;
}

void RaytracingScene::CreateBuffers(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild;
    g_DXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

    m_TLAS.Create(L"TLAS", prebuild.ResultDataMaxSizeInBytes);
    m_ScratchBuffer.Create(L"TLAS Scratch", (uint32_t)prebuild.ScratchDataSizeInBytes, 1);
    m_MaxInstances = inputs.NumDescs;
}
