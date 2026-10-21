#include "Model/Raytracing/BottomLevelAS.h"
#include "Model/Model.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Descriptor/DescriptorHeap.h"
#include "Core/Graphics/Resource/GpuBuffer.h"

using namespace Math;
using namespace Graphics;

namespace
{
    // RaytracingScene.hlsli의 BLASGeometry와 같은 layout이다.
    struct BLASGeometry
    {
        uint32_t indexOffset;
        uint32_t indexSize;
        uint32_t vertexOffset;
        uint32_t vertexStride;
        uint32_t normalOffset;
        uint32_t tangentOffset;
        uint32_t uv0Offset;
        uint32_t uv1Offset;
        uint32_t material;
        uint32_t textures;
        uint32_t samplers;
    };
    static_assert(sizeof(BLASGeometry) == 44, "RaytracingScene.hlsli의 kBLASGeometryStride와 같아야 한다");

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
            // alpha test material은 OPAQUE flag를 빼서 traversal 중에 shader가 alpha로 hit인지 정하게 한다.
            // 같은 triangle을 여러 번 판정해도 결과가 같으므로 NO_DUPLICATE_ANYHIT_INVOCATION은 쓰지 않는다.
            geometry.Flags = (mesh.psoFlags & PSOFlags::kAlphaTest) ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

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

    // DescribeGeometries와 같은 draw 순서로, shader가 각 geometry의 triangle과 material을 읽을 위치를 붙인다.
    void DescribeGeometriesForShader(const Mesh& mesh, std::vector<BLASGeometry>& geometries)
    {
        BLASGeometry geometry = {};
        geometry.indexSize = mesh.ibFormat == DXGI_FORMAT_R32_UINT ? 4 : 2;
        geometry.vertexStride = mesh.vbStride;
        geometry.material = mesh.materialCBV;
        geometry.textures = mesh.srvTable;
        geometry.samplers = mesh.samplerTable;

        // attribute는 Renderer::GetPSO가 psoFlags로 만드는 input layout의 순서로 놓인다.
        const uint32_t kPackedAttributeSize = 4;
        uint32_t offset = sizeof(XMFLOAT3);
        geometry.normalOffset = offset;
        offset += kPackedAttributeSize;
        if (mesh.psoFlags & PSOFlags::kHasTangent)
        {
            geometry.tangentOffset = offset;
            offset += kPackedAttributeSize;
        }
        if (mesh.psoFlags & PSOFlags::kHasUV0)
        {
            geometry.uv0Offset = offset;
            offset += kPackedAttributeSize;
        }
        if (mesh.psoFlags & PSOFlags::kHasUV1)
            geometry.uv1Offset = offset;

        for (uint32_t i = 0; i < mesh.numDraws; ++i)
        {
            const Mesh::Draw& draw = mesh.draw[i];
            geometry.indexOffset = mesh.ibOffset + draw.startIndex * geometry.indexSize;
            geometry.vertexOffset = mesh.vbOffset + draw.baseVertex * mesh.vbStride;
            geometries.push_back(geometry);
        }
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

        // glTF spec과 다르다: BLEND material은 alpha만큼 뒤를 가려야 하지만 ray는 BLEND mesh를 보지 않는다.
        // 일부만 가리는 hit를 다루려면 closest hit와 visibility가 돌려주는 값의 형태가 바뀌므로 투명한 물체를 shading할 때 정한다.
        // 그때까지는 BLEND mesh를 depth와 shadow map에 그리지 않는 raster와 같게 둔다.
        if (mesh.psoFlags & PSOFlags::kAlphaBlend)
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

void CreateBLASGeometries(Model& model)
{
    if (model.m_BLASBuffer.GetGpuVirtualAddress() == D3D12_GPU_VIRTUAL_ADDRESS_NULL)
        return;

    std::vector<BLASGeometry> geometries;
    model.m_MeshFirstBLASGeometry.assign(model.m_NumMeshes, 0);

    const uint8_t* pMesh = model.m_MeshData.get();
    for (uint32_t i = 0; i < model.m_NumMeshes; ++i)
    {
        const Mesh& mesh = *(const Mesh*)pMesh;
        pMesh += sizeof(Mesh) + (mesh.numDraws - 1) * sizeof(Mesh::Draw);

        if (model.m_MeshBLAS[i] == D3D12_GPU_VIRTUAL_ADDRESS_NULL)
            continue;

        model.m_MeshFirstBLASGeometry[i] = (uint32_t)geometries.size();
        DescribeGeometriesForShader(mesh, geometries);
    }

    model.m_BLASGeometries.Create(L"BLAS Geometries", (uint32_t)geometries.size(), sizeof(BLASGeometry), geometries.data());

    const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] =
    {
        model.m_DataBuffer.GetSRV(), model.m_MaterialConstants.GetSRV(), model.m_BLASGeometries.GetSRV()
    };
    DescriptorHandle firstSRV = Renderer::s_TextureHeap.Alloc(_countof(srvs));
    model.m_RaytracingSRVs = Renderer::s_TextureHeap.GetOffsetOfHandle(firstSRV);
    for (uint32_t i = 0; i < _countof(srvs); ++i)
    {
        g_Device->CopyDescriptorsSimple(1, firstSRV + i * Renderer::s_TextureHeap.GetDescriptorSize(), srvs[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}
