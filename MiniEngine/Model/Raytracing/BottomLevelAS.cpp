#include "Model/Raytracing/BottomLevelAS.h"
#include "Model/Model.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/GpuBuffer.h"

using namespace Graphics;

namespace
{
    // draw 하나가 geometry 하나가 된다. draw의 index는 baseVertex 기준이므로 vertex buffer 시작을 baseVertex로 옮긴다.
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> DescribeGeometries(const Mesh& mesh, D3D12_GPU_VIRTUAL_ADDRESS dataBuffer)
    {
        const uint32_t indexSize = mesh.ibFormat == DXGI_FORMAT_R32_UINT ? 4 : 2;
        const uint32_t vertexCount = mesh.vbSize / mesh.vbStride;

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(mesh.numDraws);
        for (uint32_t i = 0; i < mesh.numDraws; ++i)
        {
            const Mesh::Draw& draw = mesh.draw[i];
            D3D12_RAYTRACING_GEOMETRY_DESC& geometry = geometries[i];
            D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles = geometry.Triangles;

            geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            // alpha test material도 아직은 ray가 통과하지 못하는 opaque로 둔다.
            geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

            // position은 vertex의 맨 앞에 있다.
            triangles.VertexBuffer.StartAddress = dataBuffer + mesh.vbOffset + draw.baseVertex * mesh.vbStride;
            triangles.VertexBuffer.StrideInBytes = mesh.vbStride;
            triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            triangles.VertexCount = vertexCount - draw.baseVertex;
            triangles.IndexBuffer = dataBuffer + mesh.ibOffset + draw.startIndex * indexSize;
            triangles.IndexFormat = (DXGI_FORMAT)mesh.ibFormat;
            triangles.IndexCount = draw.primCount;
        }
        return geometries;
    }
}

void BuildBottomLevelAS(Model& model)
{
    const D3D12_GPU_VIRTUAL_ADDRESS dataBuffer = model.m_DataBuffer.GetGpuVirtualAddress();
    model.m_MeshBLAS.assign(model.m_NumMeshes, D3D12_GPU_VIRTUAL_ADDRESS_NULL);

    std::vector<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> geometries(model.m_NumMeshes);
    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> builds(model.m_NumMeshes);
    uint64_t blasBufferSize = 0;
    uint64_t scratchBufferSize = 0;

    // buffer를 만들기 전이라 BLAS와 scratch의 주소 대신 buffer 안의 offset을 먼저 적는다.
    const uint8_t* pMesh = model.m_MeshData.get();
    for (uint32_t i = 0; i < model.m_NumMeshes; ++i)
    {
        const Mesh& mesh = *(const Mesh*)pMesh;
        pMesh += sizeof(Mesh) + (mesh.numDraws - 1) * sizeof(Mesh::Draw);

        if (mesh.psoFlags & PSOFlags::kHasSkin)
            continue;

        geometries[i] = DescribeGeometries(mesh, dataBuffer);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = builds[i].Inputs;
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = (UINT)geometries[i].size();
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = geometries[i].data();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild;
        g_DXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

        builds[i].DestAccelerationStructureData = blasBufferSize;
        builds[i].ScratchAccelerationStructureData = scratchBufferSize;
        blasBufferSize += Math::AlignUp(prebuild.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
        scratchBufferSize += Math::AlignUp(prebuild.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    }

    if (blasBufferSize == 0)
        return;

    model.m_BLASBuffer.Create(L"BLAS", blasBufferSize);

    ASSERT(scratchBufferSize <= UINT32_MAX);
    ByteAddressBuffer scratchBuffer;
    scratchBuffer.Create(L"BLAS Scratch", (uint32_t)scratchBufferSize, 1);

    CommandContext& context = CommandContext::Begin(L"Build BLAS");
    context.TransitionResource(scratchBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // build마다 scratch 구간이 겹치지 않으므로 build 사이에 barrier가 필요 없다.
    for (uint32_t i = 0; i < model.m_NumMeshes; ++i)
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& build = builds[i];
        if (build.Inputs.NumDescs == 0)
            continue;

        build.DestAccelerationStructureData += model.m_BLASBuffer.GetGpuVirtualAddress();
        build.ScratchAccelerationStructureData += scratchBuffer.GetGpuVirtualAddress();
        context.BuildRaytracingAccelerationStructure(build);

        model.m_MeshBLAS[i] = build.DestAccelerationStructureData;
    }

    // scratch buffer를 해제하기 전에 build가 끝나기를 기다린다.
    context.Finish(true);
}
