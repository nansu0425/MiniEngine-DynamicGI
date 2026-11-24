#include "Model/DynamicGI/SelectedProbeGuide.h"
#include "Model/DynamicGI/ProbeGuide.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Profiling/EngineProfiling.h"

#include "Output/Model/CompiledShaders/SelectedProbeGuideCS.h"

namespace
{
    enum RootParameter { kConstants, kProbesGuide, kOutput, kNumRootParameters };

    struct Constants
    {
        uint32_t probe;
        uint32_t hasProbe;
    };

    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Selected Probe Guide");
    ColorBuffer s_SelectedProbeGuide;
}

void SelectedProbeGuide::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters, 0);
    s_RootSignature[kConstants].InitAsConstants(0, sizeof(Constants) / sizeof(uint32_t));
    s_RootSignature[kProbesGuide].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    s_RootSignature[kOutput].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature.Finalize(L"Selected Probe Guide");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pSelectedProbeGuideCS, sizeof(g_pSelectedProbeGuideCS));
    s_PSO.Finalize();

    s_SelectedProbeGuide.Create(L"Selected Probe Guide", kProbeGuideTexelsPerSide, kProbeGuideTexelsPerSide, 1, DXGI_FORMAT_R32G32_FLOAT);
}

void SelectedProbeGuide::Shutdown(void)
{
    s_SelectedProbeGuide.Destroy();
}

void SelectedProbeGuide::Update(ComputeContext& context, ColorBuffer& probesGuide, std::optional<uint32_t> probe)
{
    if (probesGuide.GetResource() == nullptr)
        return;

    ScopedTimer _prof(L"Selected Probe Guide", context);

    const Constants constants = { probe.value_or(0), probe ? 1u : 0u };
    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(probesGuide, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(s_SelectedProbeGuide, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetConstantArray(kConstants, sizeof(constants) / sizeof(uint32_t), &constants);
    context.SetDynamicDescriptor(kProbesGuide, 0, probesGuide.GetSRV());
    context.SetDynamicDescriptor(kOutput, 0, s_SelectedProbeGuide.GetUAV());
    context.Dispatch(1, 1, 1);
    context.TransitionResource(s_SelectedProbeGuide, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

ColorBuffer& SelectedProbeGuide::GetTexture(void)
{
    return s_SelectedProbeGuide;
}
