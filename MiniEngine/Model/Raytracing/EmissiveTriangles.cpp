#include "Model/Raytracing/EmissiveTriangles.h"
#include "Model/Model.h"
#include "Model/ConstantBuffers.h"

using namespace Math;

namespace
{
    float GetLuminance(const float (&color)[3])
    {
        return 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
    }

    uint32_t LoadIndex(const uint8_t* indices, uint32_t indexSize, uint32_t index)
    {
        if (indexSize == 4)
            return ((const uint32_t*)indices)[index];
        return ((const uint16_t*)indices)[index];
    }
}

void CollectEmissiveTriangles(Model& model, const uint8_t* geometryData, const MaterialConstants* materials)
{
    model.m_EmissiveTriangles.clear();

    const uint8_t* pMesh = model.m_MeshData.get();
    for (uint32_t i = 0; i < model.m_NumMeshes; ++i)
    {
        const Mesh& mesh = *(const Mesh*)pMesh;
        pMesh += sizeof(Mesh) + (mesh.numDraws - 1) * sizeof(Mesh::Draw);

        const float emission = GetLuminance(materials[mesh.materialCBV].emissiveFactor);
        if (model.m_MeshBLAS[i] == D3D12_GPU_VIRTUAL_ADDRESS_NULL || emission <= 0.0f)
            continue;

        const uint32_t indexSize = mesh.ibFormat == DXGI_FORMAT_R32_UINT ? 4 : 2;
        for (uint32_t geometry = 0; geometry < mesh.numDraws; ++geometry)
        {
            const Mesh::Draw& draw = mesh.draw[geometry];
            const uint8_t* indices = geometryData + mesh.ibOffset + draw.startIndex * indexSize;
            const uint8_t* vertices = geometryData + mesh.vbOffset + draw.baseVertex * mesh.vbStride;
            for (uint32_t primitive = 0; primitive < draw.primCount / 3; ++primitive)
            {
                EmissiveTriangle triangle = { .mesh = i, .geometry = geometry, .primitive = primitive, .emission = emission };
                for (uint32_t corner = 0; corner < 3; ++corner)
                    triangle.positions[corner] = *(const XMFLOAT3*)(vertices + LoadIndex(indices, indexSize, primitive * 3 + corner) * mesh.vbStride);
                model.m_EmissiveTriangles.push_back(triangle);
            }
        }
    }
}
