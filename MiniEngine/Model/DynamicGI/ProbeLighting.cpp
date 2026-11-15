#include "Model/DynamicGI/ProbeLighting.h"
#include "Model/DynamicGI/GILights.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Graphics/Resource/ShadowBuffer.h"

#include "Output/Model/CompiledShaders/ProbeTraceCS.h"
#include "Output/Model/CompiledShaders/ProbeIrradianceUpdateCS.h"
#include "Output/Model/CompiledShaders/ProbeDistanceUpdateCS.h"
#include "Output/Model/CompiledShaders/ProbeBorderCS.h"
#include "Output/Model/CompiledShaders/ProbeScreenIrradianceCS.h"

#include <algorithm>
#include <random>

using namespace Math;
using namespace Graphics;

namespace
{
    constexpr uint32_t kProbeIrradianceTexelsPerSide = 8;
    constexpr uint32_t kProbeTileBorderTexels = 1;
    constexpr uint32_t kProbeAtlasGroupSize = 8;

    enum TraceRootParameter { kTraceConstants, kTraceScene, kTraceInputs = kTraceScene + RaytracingScene::kNumRootParameters, kTraceOutputs, kNumTraceRootParameters };
    enum UpdateRootParameter { kUpdateConstants, kUpdateInputs, kUpdateOutputs, kNumUpdateRootParameters };
    enum ScreenRootParameter { kScreenConstants, kScreenScene, kScreenInputs = kScreenScene + RaytracingScene::kNumRootParameters, kScreenOutputs,
        kNumScreenRootParameters };

    RootSignature s_TraceRootSignature;
    RootSignature s_UpdateRootSignature;
    RootSignature s_ScreenRootSignature;
    ComputePSO s_TracePSO(L"Probe Trace");
    ComputePSO s_IrradianceUpdatePSO(L"Probes Irradiance Update");
    ComputePSO s_DistanceUpdatePSO(L"Probes Distance Update");
    ComputePSO s_BorderPSO(L"Probe Border Copy");
    ComputePSO s_ScreenPSO(L"Probe Screen Irradiance");

    SceneHeapTable s_TraceInputs;
    SceneHeapTable s_TraceOutputs;
    SceneHeapTable s_ScreenInputs;
    SceneHeapTable s_ScreenOutputs;
    ColorBuffer s_RaySamples;
    ColorBuffer s_ProbesIrradiance;
    ColorBuffer s_ProbesDistance;
    ColorBuffer s_ScreenIrradiance;

    IrradianceProbeGrid s_Grid = {};
    uint32_t s_RaysPerProbe = 0;
    uint32_t s_DistanceTexelsPerSide = 0;
    Matrix4 s_RaySamplesRotation(kIdentity);
    bool s_ResetRequested = true;
    ProbeLighting::Progress s_Progress = {};
    std::mt19937 s_Random;

    __declspec(align(16)) struct ProbeRays
    {
        IrradianceProbeGrid grid;
        Matrix4 rotation;
        uint32_t raysPerProbe;
    };

    __declspec(align(16)) struct TraceConstants
    {
        uint32_t visibility;
        uint32_t distanceTexelsPerSide;
        GILights lights;
        ProbeRays rays;
    };

    __declspec(align(16)) struct IrradianceUpdateConstants
    {
        float blendWeight;
        ProbeRays rays;
    };

    __declspec(align(16)) struct DistanceUpdateConstants
    {
        float blendWeight;
        float distanceSharpness;
        uint32_t distanceTexelsPerSide;
        ProbeRays rays;
    };

    __declspec(align(16)) struct BorderConstants
    {
        uint32_t texelsPerSide;
        IrradianceProbeGrid grid;
    };

    __declspec(align(16)) struct ScreenConstants
    {
        uint32_t width;
        uint32_t height;
        uint32_t visibility;
        uint32_t distanceTexelsPerSide;
        IrradianceProbeGrid grid;
    };

    IrradianceProbeGrid DescribeGrid(const AxisAlignedBox& bounds, const uint32_t (&counts)[3])
    {
        const Vector3 dimensions = Max(bounds.GetDimensions(), Vector3(1e-3f));
        IrradianceProbeGrid grid = {};
        grid.spacing = dimensions / Vector3((float)counts[0], (float)counts[1], (float)counts[2]);
        grid.origin = bounds.GetMin() + grid.spacing * 0.5f;
        std::copy(std::begin(counts), std::end(counts), grid.counts);
        return grid;
    }

    bool IsSameGrid(const IrradianceProbeGrid& a, const IrradianceProbeGrid& b)
    {
        return XMVector3Equal(a.origin, b.origin) && XMVector3Equal(a.spacing, b.spacing) && std::equal(std::begin(a.counts), std::end(a.counts), b.counts);
    }

    Matrix4 MakeRandomRotation(void)
    {
        std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
        const float u1 = uniform(s_Random);
        const float u2 = XM_2PI * uniform(s_Random);
        const float u3 = XM_2PI * uniform(s_Random);
        const float a = sqrtf(1.0f - u1);
        const float b = sqrtf(u1);
        const Quaternion rotation(XMVectorSet(a * sinf(u2), a * cosf(u2), b * sinf(u3), b * cosf(u3)));
        return Matrix4(Matrix3(rotation));
    }

    uint32_t GetProbeTileSize(uint32_t texelsPerSide)
    {
        return texelsPerSide + 2 * kProbeTileBorderTexels;
    }

    void CreateProbeAtlas(ColorBuffer& atlas, const wchar_t* name, const IrradianceProbeGrid& grid, uint32_t texelsPerSide, DXGI_FORMAT format)
    {
        const uint32_t tileSize = GetProbeTileSize(texelsPerSide);
        atlas.Create(name, grid.counts[0] * grid.counts[1] * tileSize, grid.counts[2] * tileSize, 1, format);
    }

    void FitResourcesTo(const IrradianceProbeGrid& grid, uint32_t raysPerProbe, uint32_t distanceTexelsPerSide)
    {
        if (IsSameGrid(grid, s_Grid) && raysPerProbe == s_RaysPerProbe && distanceTexelsPerSide == s_DistanceTexelsPerSide)
            return;

        g_CommandManager.IdleGPU();
        CreateProbeAtlas(s_ProbesIrradiance, L"Probes Irradiance", grid, kProbeIrradianceTexelsPerSide, DXGI_FORMAT_R32G32B32A32_FLOAT);
        CreateProbeAtlas(s_ProbesDistance, L"Probes Distance", grid, distanceTexelsPerSide, DXGI_FORMAT_R32G32_FLOAT);
        s_RaySamples.Create(L"Probe Ray Samples", raysPerProbe, GetProbeCount(grid), 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
        s_Grid = grid;
        s_RaysPerProbe = raysPerProbe;
        s_DistanceTexelsPerSide = distanceTexelsPerSide;
        s_ResetRequested = true;
    }

    void CopyProbeTileBorders(ComputeContext& context, ColorBuffer& atlas, uint32_t texelsPerSide)
    {
        const BorderConstants constants = { .texelsPerSide = texelsPerSide, .grid = s_Grid };
        const uint32_t tileSize = GetProbeTileSize(texelsPerSide);
        context.SetDynamicConstantBufferView(kUpdateConstants, sizeof(constants), &constants);
        context.SetDynamicDescriptor(kUpdateOutputs, 0, atlas.GetUAV());
        context.Dispatch3D(tileSize, tileSize, GetProbeCount(s_Grid), kProbeAtlasGroupSize, kProbeAtlasGroupSize, 1);
    }

    void FitScreenIrradianceTo(uint32_t width, uint32_t height)
    {
        if (s_ScreenIrradiance.GetWidth() == width && s_ScreenIrradiance.GetHeight() == height)
            return;

        g_CommandManager.IdleGPU();
        s_ScreenIrradiance.Create(L"Probe Screen Irradiance", width, height, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
    }
}

void ProbeLighting::Initialize(void)
{
    s_TraceRootSignature.Reset(kNumTraceRootParameters, 2);
    s_TraceRootSignature[kTraceConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_TraceRootSignature, kTraceScene);
    s_TraceRootSignature[kTraceInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 3);
    s_TraceRootSignature[kTraceOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_TraceRootSignature.InitStaticSampler(0, SamplerShadowDesc);
    s_TraceRootSignature.InitStaticSampler(1, SamplerLinearClampDesc);
    s_TraceRootSignature.Finalize(L"Probe Trace");

    s_TracePSO.SetRootSignature(s_TraceRootSignature);
    s_TracePSO.SetComputeShader(g_pProbeTraceCS, sizeof(g_pProbeTraceCS));
    s_TracePSO.Finalize();

    s_UpdateRootSignature.Reset(kNumUpdateRootParameters, 0);
    s_UpdateRootSignature[kUpdateConstants].InitAsConstantBuffer(0);
    s_UpdateRootSignature[kUpdateInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    s_UpdateRootSignature[kUpdateOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_UpdateRootSignature.Finalize(L"Probes Update");

    s_IrradianceUpdatePSO.SetRootSignature(s_UpdateRootSignature);
    s_IrradianceUpdatePSO.SetComputeShader(g_pProbeIrradianceUpdateCS, sizeof(g_pProbeIrradianceUpdateCS));
    s_IrradianceUpdatePSO.Finalize();

    s_DistanceUpdatePSO.SetRootSignature(s_UpdateRootSignature);
    s_DistanceUpdatePSO.SetComputeShader(g_pProbeDistanceUpdateCS, sizeof(g_pProbeDistanceUpdateCS));
    s_DistanceUpdatePSO.Finalize();

    s_BorderPSO.SetRootSignature(s_UpdateRootSignature);
    s_BorderPSO.SetComputeShader(g_pProbeBorderCS, sizeof(g_pProbeBorderCS));
    s_BorderPSO.Finalize();

    s_ScreenRootSignature.Reset(kNumScreenRootParameters, 1);
    s_ScreenRootSignature[kScreenConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_ScreenRootSignature, kScreenScene);
    s_ScreenRootSignature[kScreenInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, kNumGBufferTargets + 2);
    s_ScreenRootSignature[kScreenOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_ScreenRootSignature.InitStaticSampler(0, SamplerLinearClampDesc);
    s_ScreenRootSignature.Finalize(L"Probe Screen Irradiance");

    s_ScreenPSO.SetRootSignature(s_ScreenRootSignature);
    s_ScreenPSO.SetComputeShader(g_pProbeScreenIrradianceCS, sizeof(g_pProbeScreenIrradianceCS));
    s_ScreenPSO.Finalize();

    s_TraceInputs.Create(3);
    s_TraceOutputs.Create(1);
    s_ScreenInputs.Create(kNumGBufferTargets + 2);
    s_ScreenOutputs.Create(1);
}

void ProbeLighting::Shutdown(void)
{
    s_RaySamples.Destroy();
    s_ProbesIrradiance.Destroy();
    s_ProbesDistance.Destroy();
    s_ScreenIrradiance.Destroy();
}

void ProbeLighting::Reset(void)
{
    s_ResetRequested = true;
}

ProbeLighting::Progress ProbeLighting::GetProgress(void)
{
    return s_Progress;
}

void ProbeLighting::Update(ComputeContext& context, const Settings& settings, const GIFrameInputs& inputs)
{
    ScopedTimer _prof(L"Probe Lighting", context);

    FitResourcesTo(DescribeGrid(inputs.modelBounds, settings.probeCounts), settings.raysPerProbe, settings.distanceTexelsPerSide);
    if (s_ResetRequested)
    {
        context.TransitionResource(s_ProbesIrradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        context.ClearUAV(s_ProbesIrradiance);
        context.TransitionResource(s_ProbesDistance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        context.ClearUAV(s_ProbesDistance);
        s_Random.seed(0);
        s_Progress.updates = 0;
        ++s_Progress.resets;
        s_ResetRequested = false;
    }

    const uint32_t probeCount = GetProbeCount(s_Grid);
    const ProbeRays rays = { s_Grid, MakeRandomRotation(), s_RaysPerProbe };
    s_RaySamplesRotation = rays.rotation;

    {
        ScopedTimer _traceProf(L"Probe Trace", context);

        const TraceConstants constants = { (uint32_t)settings.visibility, s_DistanceTexelsPerSide, DescribeGILights(inputs), rays };
        s_TraceInputs.PointTo(0, inputs.shadowMap, inputs.shadowMap.GetSRV());
        s_TraceInputs.PointTo(1, s_ProbesIrradiance, s_ProbesIrradiance.GetSRV());
        s_TraceInputs.PointTo(2, s_ProbesDistance, s_ProbesDistance.GetSRV());
        s_TraceOutputs.PointTo(0, s_RaySamples, s_RaySamples.GetUAV());

        context.SetRootSignature(s_TraceRootSignature);
        context.SetPipelineState(s_TracePSO);
        context.TransitionResource(inputs.shadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_ProbesIrradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_ProbesDistance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_RaySamples, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.SetDynamicConstantBufferView(kTraceConstants, sizeof(constants), &constants);
        inputs.scene.Bind(context, kTraceScene);
        s_TraceInputs.Bind(context, kTraceInputs);
        s_TraceOutputs.Bind(context, kTraceOutputs);
        context.Dispatch2D(s_RaysPerProbe, probeCount);
    }

    {
        ScopedTimer _updateProf(L"Probes Update", context);

        const float blendWeight = std::max(1.0f - settings.hysteresis, 1.0f / (s_Progress.updates + 1));
        context.SetRootSignature(s_UpdateRootSignature);
        context.TransitionResource(s_RaySamples, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_ProbesIrradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.TransitionResource(s_ProbesDistance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.SetDynamicDescriptor(kUpdateInputs, 0, s_RaySamples.GetSRV());

        const IrradianceUpdateConstants irradianceConstants = { .blendWeight = blendWeight, .rays = rays };
        context.SetPipelineState(s_IrradianceUpdatePSO);
        context.SetDynamicConstantBufferView(kUpdateConstants, sizeof(irradianceConstants), &irradianceConstants);
        context.SetDynamicDescriptor(kUpdateOutputs, 0, s_ProbesIrradiance.GetUAV());
        context.Dispatch(probeCount);

        const DistanceUpdateConstants distanceConstants = {
            .blendWeight = blendWeight,
            .distanceSharpness = settings.distanceSharpness,
            .distanceTexelsPerSide = s_DistanceTexelsPerSide,
            .rays = rays,
        };
        context.SetPipelineState(s_DistanceUpdatePSO);
        context.SetDynamicConstantBufferView(kUpdateConstants, sizeof(distanceConstants), &distanceConstants);
        context.SetDynamicDescriptor(kUpdateOutputs, 0, s_ProbesDistance.GetUAV());
        context.Dispatch3D(s_DistanceTexelsPerSide, s_DistanceTexelsPerSide, probeCount, kProbeAtlasGroupSize, kProbeAtlasGroupSize, 1);

        context.InsertUAVBarrier(s_ProbesIrradiance);
        context.InsertUAVBarrier(s_ProbesDistance);
        context.SetPipelineState(s_BorderPSO);
        CopyProbeTileBorders(context, s_ProbesIrradiance, kProbeIrradianceTexelsPerSide);
        CopyProbeTileBorders(context, s_ProbesDistance, s_DistanceTexelsPerSide);
    }

    ++s_Progress.updates;
}

void ProbeLighting::EstimateScreenIrradiance(ComputeContext& context, Visibility visibility, const GIFrameInputs& inputs, uint32_t width,
    uint32_t height)
{
    ScopedTimer _prof(L"Probe Screen Irradiance", context);

    FitScreenIrradianceTo(width, height);

    for (uint32_t i = 0; i < kNumGBufferTargets; ++i)
        s_ScreenInputs.PointTo(i, g_GBuffer[i], g_GBuffer[i].GetSRV());
    s_ScreenInputs.PointTo(kNumGBufferTargets, s_ProbesIrradiance, s_ProbesIrradiance.GetSRV());
    s_ScreenInputs.PointTo(kNumGBufferTargets + 1, s_ProbesDistance, s_ProbesDistance.GetSRV());
    s_ScreenOutputs.PointTo(0, s_ScreenIrradiance, s_ScreenIrradiance.GetUAV());

    const ScreenConstants constants = { width, height, (uint32_t)visibility, s_DistanceTexelsPerSide, s_Grid };
    context.SetRootSignature(s_ScreenRootSignature);
    context.SetPipelineState(s_ScreenPSO);
    for (ColorBuffer& target : g_GBuffer)
        context.TransitionResource(target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(s_ProbesIrradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(s_ProbesDistance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(s_ScreenIrradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kScreenConstants, sizeof(constants), &constants);
    inputs.scene.Bind(context, kScreenScene);
    s_ScreenInputs.Bind(context, kScreenInputs);
    s_ScreenOutputs.Bind(context, kScreenOutputs);
    context.Dispatch2D(width, height);
}

ProbeLighting::Probes ProbeLighting::GetProbes(void)
{
    return {
        .grid = s_Grid,
        .irradiance = s_ProbesIrradiance,
        .distance = s_ProbesDistance,
        .distanceTexelsPerSide = s_DistanceTexelsPerSide,
        .raySamples = s_RaySamples,
        .raySamplesRotation = s_RaySamplesRotation,
        .raysPerProbe = s_RaysPerProbe,
    };
}

ColorBuffer& ProbeLighting::GetScreenIrradiance(void)
{
    return s_ScreenIrradiance;
}
