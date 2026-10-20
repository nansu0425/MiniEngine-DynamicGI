#pragma once

#include "Core/Graphics/Resource/AccelerationStructureBuffer.h"
#include "Core/Graphics/Resource/GpuBuffer.h"

class CommandContext;
class ModelInstance;

// ray를 쏘는 대상이 되는 scene의 TLAS를 갖는다.
// 매 frame model instance를 Update한 뒤 AddModelInstance로 넣고 Build한다.
// model instance의 mesh 하나가 BLAS instance 하나가 되고, 넣은 순서가 TLAS의 instance index가 된다.
class RaytracingScene
{
public:
    void AddModelInstance(const ModelInstance& modelInstance);
    // 모은 BLAS instance로 TLAS를 다시 build하고, 다음 frame을 위해 비운다.
    void Build(CommandContext& context);
    void Destroy();

    const AccelerationStructureBuffer& GetTLAS() const { return m_TLAS; }

private:
    void CreateBuffers(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs);

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_Instances;
    AccelerationStructureBuffer m_TLAS;
    ByteAddressBuffer m_ScratchBuffer;
    uint32_t m_MaxInstances = 0;
};
