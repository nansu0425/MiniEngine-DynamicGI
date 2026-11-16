#include "Model/DynamicGI/ProbeContribution.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/GpuBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"

#include "Output/Model/CompiledShaders/ProbeContributionCS.h"

using namespace Graphics;

namespace
{
    static_assert(sizeof(ProbeContribution::Surface) == 424, "ProbeContribution.hlsli의 SurfaceProbeContribution과 크기가 다르다");

    enum RootParameter { kConstants, kScene, kInputs = kScene + RaytracingScene::kNumRootParameters, kOutputs, kNumRootParameters };

    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Probe Contribution");
    SceneHeapTable s_Inputs;
    SceneHeapTable s_Outputs;
    StructuredBuffer s_Surface;
    ReadbackBuffer s_SurfaceReadback;

    __declspec(align(16)) struct Constants
    {
        DirectX::XMUINT2 pixel;
        uint32_t capturesSurface;
        uint32_t visibility;
        uint32_t distanceTexelsPerSide;
        IrradianceProbeGrid grid;
    };

    struct PendingReadback
    {
        uint64_t fence;
        uint32_t captureId;
    };

    std::optional<DirectX::XMUINT2> s_PixelToCapture;
    uint32_t s_LastCaptureId = 0;
    std::optional<PendingReadback> s_PendingReadback;
    std::optional<ProbeContribution::Readback> s_Readback;

    void CollectFinishedReadback(void)
    {
        if (!s_PendingReadback || !g_CommandManager.IsFenceComplete(s_PendingReadback->fence))
            return;

        ProbeContribution::Readback readback = { s_PendingReadback->captureId, {} };
        std::memcpy(&readback.surface, s_SurfaceReadback.Map(), sizeof(readback.surface));
        s_SurfaceReadback.Unmap();
        s_Readback = readback;
        s_PendingReadback.reset();
    }
}

void ProbeContribution::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters, 1);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_RootSignature, kScene);
    s_RootSignature[kInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, kNumGBufferTargets + 2);
    s_RootSignature[kOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature.InitStaticSampler(0, SamplerLinearClampDesc);
    s_RootSignature.Finalize(L"Probe Contribution");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pProbeContributionCS, sizeof(g_pProbeContributionCS));
    s_PSO.Finalize();

    s_Inputs.Create(kNumGBufferTargets + 2);
    s_Outputs.Create(1);
    s_Surface.Create(L"Probe Contribution Surface", 1, sizeof(Surface));
    s_SurfaceReadback.Create(L"Probe Contribution Surface Readback", 1, sizeof(Surface));
}

void ProbeContribution::Shutdown(void)
{
    s_Surface.Destroy();
    s_SurfaceReadback.Destroy();
}

uint32_t ProbeContribution::CaptureSurface(DirectX::XMUINT2 scenePixel)
{
    s_PixelToCapture = scenePixel;
    return ++s_LastCaptureId;
}

void ProbeContribution::Measure(ProbeLighting::Visibility visibility, const RaytracingScene& scene)
{
    CollectFinishedReadback();

    const ProbeLighting::Probes probes = ProbeLighting::GetProbes();
    ComputeContext& context = ComputeContext::Begin(L"Probe Contribution");
    bool readsBack = false;
    {
        ScopedTimer _prof(L"Probe Contribution", context);

        for (uint32_t i = 0; i < kNumGBufferTargets; ++i)
            s_Inputs.PointTo(i, g_GBuffer[i], g_GBuffer[i].GetSRV());
        s_Inputs.PointTo(kNumGBufferTargets, probes.irradiance, probes.irradiance.GetSRV());
        s_Inputs.PointTo(kNumGBufferTargets + 1, probes.distance, probes.distance.GetSRV());
        s_Outputs.PointTo(0, s_Surface, s_Surface.GetUAV());

        const Constants constants = {
            .pixel = s_PixelToCapture.value_or(DirectX::XMUINT2(0, 0)),
            .capturesSurface = s_PixelToCapture ? 1u : 0u,
            .visibility = (uint32_t)visibility,
            .distanceTexelsPerSide = probes.distanceTexelsPerSide,
            .grid = probes.grid,
        };
        context.SetRootSignature(s_RootSignature);
        context.SetPipelineState(s_PSO);
        for (ColorBuffer& target : g_GBuffer)
            context.TransitionResource(target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(probes.irradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(probes.distance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_Surface, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
        scene.Bind(context, kScene);
        s_Inputs.Bind(context, kInputs);
        s_Outputs.Bind(context, kOutputs);
        context.Dispatch(1, 1, 1);
        s_PixelToCapture.reset();

        readsBack = !s_PendingReadback;
        if (readsBack)
            context.CopyBuffer(s_SurfaceReadback, s_Surface);
    }
    const uint64_t fence = context.Finish();
    if (readsBack)
        s_PendingReadback = PendingReadback{ fence, s_LastCaptureId };
}

const std::optional<ProbeContribution::Readback>& ProbeContribution::GetReadback(void)
{
    CollectFinishedReadback();
    return s_Readback;
}

StructuredBuffer& ProbeContribution::GetSurfaceBuffer(void)
{
    return s_Surface;
}
