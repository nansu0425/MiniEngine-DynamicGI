#include "Model/DynamicGI/ProbeLighting.h"
#include "Model/DynamicGI/GILights.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/ShadowBuffer.h"

#include "Output/Model/CompiledShaders/ProbeTraceCS.h"
#include "Output/Model/CompiledShaders/ProbeUpdateCS.h"
#include "Output/Model/CompiledShaders/ProbeBorderCS.h"

#include <algorithm>
#include <random>

using namespace Math;
using namespace Graphics;

namespace
{
    constexpr uint32_t kProbeTexelsPerSide = 8;
    constexpr uint32_t kProbeTileSize = kProbeTexelsPerSide + 2;

    enum TraceRootParameter { kTraceConstants, kTraceScene, kTraceInputs = kTraceScene + RaytracingScene::kNumRootParameters, kTraceOutputs, kNumTraceRootParameters };
    enum UpdateRootParameter { kUpdateConstants, kUpdateInputs, kUpdateOutputs, kNumUpdateRootParameters };

    RootSignature s_TraceRootSignature;
    RootSignature s_UpdateRootSignature;
    ComputePSO s_TracePSO(L"Probe Trace");
    ComputePSO s_UpdatePSO(L"Probes Irradiance Update");
    ComputePSO s_BorderPSO(L"Probe Border Copy");

    SceneHeapTable s_TraceInputs;
    SceneHeapTable s_TraceOutputs;
    ColorBuffer s_RayRadiance;
    ColorBuffer s_ProbesIrradiance;

    IrradianceProbeGrid s_Grid = {};
    uint32_t s_RaysPerProbe = 0;
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
        GILights lights;
        ProbeRays rays;
    };

    __declspec(align(16)) struct UpdateConstants
    {
        float blendWeight;
        ProbeRays rays;
    };

    uint32_t GetProbeCount(const IrradianceProbeGrid& grid)
    {
        return grid.counts[0] * grid.counts[1] * grid.counts[2];
    }

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

    void FitResourcesTo(const IrradianceProbeGrid& grid, uint32_t raysPerProbe)
    {
        if (IsSameGrid(grid, s_Grid) && raysPerProbe == s_RaysPerProbe)
            return;

        g_CommandManager.IdleGPU();
        s_ProbesIrradiance.Create(L"Probes Irradiance", grid.counts[0] * grid.counts[1] * kProbeTileSize, grid.counts[2] * kProbeTileSize, 1,
            DXGI_FORMAT_R32G32B32A32_FLOAT);
        s_RayRadiance.Create(L"Probe Ray Radiance", raysPerProbe, GetProbeCount(grid), 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
        s_Grid = grid;
        s_RaysPerProbe = raysPerProbe;
        s_ResetRequested = true;
    }
}

void ProbeLighting::Initialize(void)
{
    s_TraceRootSignature.Reset(kNumTraceRootParameters, 2);
    s_TraceRootSignature[kTraceConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_TraceRootSignature, kTraceScene);
    s_TraceRootSignature[kTraceInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
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
    s_UpdateRootSignature.Finalize(L"Probes Irradiance Update");

    s_UpdatePSO.SetRootSignature(s_UpdateRootSignature);
    s_UpdatePSO.SetComputeShader(g_pProbeUpdateCS, sizeof(g_pProbeUpdateCS));
    s_UpdatePSO.Finalize();

    s_BorderPSO.SetRootSignature(s_UpdateRootSignature);
    s_BorderPSO.SetComputeShader(g_pProbeBorderCS, sizeof(g_pProbeBorderCS));
    s_BorderPSO.Finalize();

    s_TraceInputs.Create(2);
    s_TraceOutputs.Create(1);
}

void ProbeLighting::Shutdown(void)
{
    s_RayRadiance.Destroy();
    s_ProbesIrradiance.Destroy();
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

    FitResourcesTo(DescribeGrid(inputs.modelBounds, settings.probeCounts), settings.raysPerProbe);
    if (s_ResetRequested)
    {
        context.TransitionResource(s_ProbesIrradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        context.ClearUAV(s_ProbesIrradiance);
        s_Random.seed(0);
        s_Progress.updates = 0;
        ++s_Progress.resets;
        s_ResetRequested = false;
    }

    const uint32_t probeCount = GetProbeCount(s_Grid);
    const ProbeRays rays = { s_Grid, MakeRandomRotation(), s_RaysPerProbe };

    {
        ScopedTimer _traceProf(L"Probe Trace", context);

        const TraceConstants constants = { DescribeGILights(inputs), rays };
        s_TraceInputs.PointTo(0, inputs.shadowMap, inputs.shadowMap.GetSRV());
        s_TraceInputs.PointTo(1, s_ProbesIrradiance, s_ProbesIrradiance.GetSRV());
        s_TraceOutputs.PointTo(0, s_RayRadiance, s_RayRadiance.GetUAV());

        context.SetRootSignature(s_TraceRootSignature);
        context.SetPipelineState(s_TracePSO);
        context.TransitionResource(inputs.shadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_ProbesIrradiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_RayRadiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.SetDynamicConstantBufferView(kTraceConstants, sizeof(constants), &constants);
        inputs.scene.Bind(context, kTraceScene);
        s_TraceInputs.Bind(context, kTraceInputs);
        s_TraceOutputs.Bind(context, kTraceOutputs);
        context.Dispatch2D(s_RaysPerProbe, probeCount);
    }

    {
        ScopedTimer _updateProf(L"Probes Irradiance Update", context);

        const UpdateConstants constants = { std::max(1.0f - settings.hysteresis, 1.0f / (s_Progress.updates + 1)), rays };
        context.SetRootSignature(s_UpdateRootSignature);
        context.SetPipelineState(s_UpdatePSO);
        context.TransitionResource(s_RayRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_ProbesIrradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.SetDynamicConstantBufferView(kUpdateConstants, sizeof(constants), &constants);
        context.SetDynamicDescriptor(kUpdateInputs, 0, s_RayRadiance.GetSRV());
        context.SetDynamicDescriptor(kUpdateOutputs, 0, s_ProbesIrradiance.GetUAV());
        context.Dispatch(probeCount);

        context.InsertUAVBarrier(s_ProbesIrradiance);
        context.SetPipelineState(s_BorderPSO);
        context.SetDynamicConstantBufferView(kUpdateConstants, sizeof(s_Grid), &s_Grid);
        context.Dispatch(probeCount);
    }

    ++s_Progress.updates;
}

ColorBuffer& ProbeLighting::GetProbesIrradiance(void)
{
    return s_ProbesIrradiance;
}

ColorBuffer& ProbeLighting::GetRayRadiance(void)
{
    return s_RayRadiance;
}

const IrradianceProbeGrid& ProbeLighting::GetGrid(void)
{
    return s_Grid;
}
