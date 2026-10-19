#include "Model/Raytracing/RayQuerySmokeTest.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/GpuBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"

#include "Output/Model/CompiledShaders/RayQuerySmokeTestCS.h"

using namespace Graphics;

bool RayQuerySmokeTest::Run(void)
{
    constexpr uint32_t kThreadCount = 64;

    RootSignature rootSig(2);
    rootSig[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    rootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    rootSig.Finalize(L"RayQuerySmokeTest");

    ComputePSO pso(L"RayQuerySmokeTest");
    pso.SetRootSignature(rootSig);
    pso.SetComputeShader(g_pRayQuerySmokeTestCS, sizeof(g_pRayQuerySmokeTestCS));
    pso.Finalize();

    D3D12_SHADER_RESOURCE_VIEW_DESC nullSceneDesc = {};
    nullSceneDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    nullSceneDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSceneDesc.RaytracingAccelerationStructure.Location = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE nullScene = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_Device->CreateShaderResourceView(nullptr, &nullSceneDesc, nullScene);

    ByteAddressBuffer result;
    result.Create(L"RayQuerySmokeTest Result", kThreadCount, sizeof(uint32_t));

    ReadbackBuffer readback;
    readback.Create(L"RayQuerySmokeTest Readback", kThreadCount, sizeof(uint32_t));

    ComputeContext& context = ComputeContext::Begin(L"RayQuerySmokeTest");
    context.SetRootSignature(rootSig);
    context.SetPipelineState(pso);
    context.TransitionResource(result, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    context.SetDynamicDescriptor(0, 0, nullScene);
    context.SetDynamicDescriptor(1, 0, result.GetUAV());
    context.Dispatch1D(kThreadCount, 64);
    context.CopyBuffer(readback, result);
    context.Finish(true);

    const uint32_t* missFlags = (const uint32_t*)readback.Map();
    uint32_t missCount = 0;
    for (uint32_t i = 0; i < kThreadCount; ++i)
        missCount += missFlags[i];
    readback.Unmap();

    const bool passed = missCount == kThreadCount;
    Utility::Printf("RayQuery smoke test: %s (%u/%u threads missed)\n", passed ? "PASS" : "FAIL", missCount, kThreadCount);
    return passed;
}
