#pragma once

#include "Core/Graphics/Resource/AccelerationStructureBuffer.h"
#include "Core/Graphics/Resource/GpuBuffer.h"

class CommandContext;
class ComputeContext;
class RootSignature;
class ModelInstance;

// ray를 쏘는 대상이 되는 scene의 TLAS와, BLAS instance마다 그 model의 data를 shader가 찾을 위치를 갖는다.
// 매 frame model instance를 Update한 뒤 AddModelInstance로 넣고 Build한다.
// model instance의 mesh 하나가 BLAS instance 하나가 되고, 넣은 순서가 TLAS 안에서 그 BLAS instance의 순서가 된다.
// shader는 RaytracingScene.hlsli가 선언한 resource로 scene을 읽는다.
class RaytracingScene
{
public:
    static const UINT kNumRootParameters = 5;

    static void InitRootParameters(RootSignature& rootSignature, UINT firstRootIndex);

    void AddModelInstance(const ModelInstance& modelInstance);
    // 모은 BLAS instance로 TLAS를 다시 build하고, 다음 frame을 위해 비운다.
    void Build(CommandContext& context);
    void Destroy();

    // 이 context의 descriptor heap이 Renderer의 scene descriptor heap으로 바뀌므로 다른 table도 그 heap에서 가리켜야 한다.
    void Bind(ComputeContext& context, UINT firstRootIndex) const;

private:
    // RaytracingScene.hlsli의 BLASInstance와 같은 layout이다.
    struct BLASInstance
    {
        uint32_t modelSRVs;
        uint32_t firstGeometry;
    };

    void CreateBuffers(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs);

    // 두 vector는 같은 BLAS instance를 같은 순서로 담는다.
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_InstanceDescs;
    std::vector<BLASInstance> m_BLASInstances;
    AccelerationStructureBuffer m_TLAS;
    ByteAddressBuffer m_ScratchBuffer;
    StructuredBuffer m_BLASInstanceBuffer;
    uint32_t m_MaxInstances = 0;
};
