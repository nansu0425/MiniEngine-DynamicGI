#include "Model/DynamicGI/PathTracedLighting.h"
#include "Model/DynamicGI/GILights.h"
#include "Model/Raytracing/CameraRays.h"
#include "Core/Camera/Camera.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/BufferManager.h"

#include "Output/Model/CompiledShaders/PathTracedIndirectCS.h"

#include <algorithm>

using namespace Math;
using namespace Graphics;

namespace
{
    enum RootParameter { kConstants, kScene, kGBuffer = kScene + RaytracingScene::kNumRootParameters, kOutputs, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_IndirectPSO(L"Path Traced Indirect");

    SceneHeapTable s_GBufferSRVs;
    SceneHeapTable s_OutputUAVs;
    ColorBuffer s_IndirectRadiance;

    struct AccumulationConditions
    {
        CameraRays rays;
        PathTracedLighting::Settings settings;
        Vector3 lightDirection;
        Vector3 lightIlluminance;
        Vector3 skyRadiance;
    };

    // samplesPerFrame과 targetSamples는 쌓는 속도와 양만 정하므로 비교하지 않는다.
    bool CanKeepAccumulating(const PathTracedLighting::Settings& a, const PathTracedLighting::Settings& b)
    {
        return a.seed == b.seed && a.maxHits == b.maxHits && a.russianRoulette == b.russianRoulette;
    }

    bool CanKeepAccumulating(const AccumulationConditions& a, const AccumulationConditions& b)
    {
        return XMVector3Equal(a.rays.origin, b.rays.origin) &&
            XMVector3Equal(a.rays.viewportCornerDirection, b.rays.viewportCornerDirection) &&
            XMVector3Equal(a.rays.pixelStepRight, b.rays.pixelStepRight) &&
            XMVector3Equal(a.rays.pixelStepDown, b.rays.pixelStepDown) &&
            XMVector3Equal(a.lightDirection, b.lightDirection) && XMVector3Equal(a.lightIlluminance, b.lightIlluminance) &&
            XMVector3Equal(a.skyRadiance, b.skyRadiance) && CanKeepAccumulating(a.settings, b.settings);
    }

    bool s_ResetRequested = true;
    AccumulationConditions s_CurrentAccumConditions;
    PathTracedLighting::Progress s_Progress = {};

    __declspec(align(16)) struct Constants
    {
        GILights Lights;
        // HLSL의 cbuffer는 float3 뒤의 scalar를 같은 16 byte에 채우므로 XMFLOAT3로 둔다.
        XMFLOAT3 ViewerPos;
        uint32_t Width;
        uint32_t Height;
        uint32_t Seed;
        uint32_t SamplesBeforeThisFrame;
        uint32_t SamplesThisFrame;
        uint32_t MaxHits;
        uint32_t RussianRoulette;
    };

    // SceneHeapTable.h의 규칙대로, 앞 frame의 dispatch가 읽고 있을 수 있는 buffer는 GPU를 기다린 뒤에 다시 만든다.
    void FitAccumulationTo(uint32_t width, uint32_t height)
    {
        if (s_IndirectRadiance.GetWidth() == width && s_IndirectRadiance.GetHeight() == height)
            return;

        g_CommandManager.IdleGPU();
        s_IndirectRadiance.Create(L"Path Traced Indirect Radiance", width, height, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
        s_ResetRequested = true;
    }
}

void PathTracedLighting::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_RootSignature, kScene);
    s_RootSignature[kGBuffer].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, kNumGBufferTargets);
    s_RootSignature[kOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature.Finalize(L"Path Traced Lighting");

    s_IndirectPSO.SetRootSignature(s_RootSignature);
    s_IndirectPSO.SetComputeShader(g_pPathTracedIndirectCS, sizeof(g_pPathTracedIndirectCS));
    s_IndirectPSO.Finalize();

    s_GBufferSRVs.Create(kNumGBufferTargets);
    s_OutputUAVs.Create(1);
}

void PathTracedLighting::Shutdown(void)
{
    s_IndirectRadiance.Destroy();
}

void PathTracedLighting::Reset(void)
{
    s_ResetRequested = true;
}

PathTracedLighting::Progress PathTracedLighting::GetProgress(void)
{
    return s_Progress;
}

void PathTracedLighting::Accumulate(ComputeContext& context, const Settings& settings, const GIFrameInputs& inputs, uint32_t width, uint32_t height)
{
    ScopedTimer _prof(L"Path Traced Indirect", context);

    FitAccumulationTo(width, height);

    const AccumulationConditions conditions = {
        .rays = DescribeCameraRays(inputs.camera, inputs.rasterViewport),
        .settings = settings,
        .lightDirection = inputs.lightDirection,
        .lightIlluminance = inputs.lightIlluminance,
        .skyRadiance = inputs.skyRadiance,
    };
    if (s_ResetRequested || !CanKeepAccumulating(conditions, s_CurrentAccumConditions))
    {
        s_CurrentAccumConditions = conditions;
        s_Progress.samples = 0;
        ++s_Progress.resets;
        s_ResetRequested = false;
    }

    Constants constants;
    constants.Lights = DescribeGILights(inputs);
    XMStoreFloat3(&constants.ViewerPos, inputs.camera.GetPosition());
    constants.Width = width;
    constants.Height = height;
    constants.Seed = settings.seed;
    constants.SamplesBeforeThisFrame = s_Progress.samples;
    constants.SamplesThisFrame = s_Progress.samples < settings.targetSamples ?
        std::min(settings.samplesPerFrame, settings.targetSamples - s_Progress.samples) : 0;
    constants.MaxHits = settings.maxHits;
    constants.RussianRoulette = settings.russianRoulette ? 1 : 0;

    if (constants.SamplesThisFrame == 0)
        return;

    for (uint32_t i = 0; i < kNumGBufferTargets; ++i)
        s_GBufferSRVs.PointTo(i, g_GBuffer[i], g_GBuffer[i].GetSRV());
    s_OutputUAVs.PointTo(0, s_IndirectRadiance, s_IndirectRadiance.GetUAV());

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_IndirectPSO);
    for (ColorBuffer& target : g_GBuffer)
        context.TransitionResource(target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(s_IndirectRadiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    inputs.scene.Bind(context, kScene);
    s_GBufferSRVs.Bind(context, kGBuffer);
    s_OutputUAVs.Bind(context, kOutputs);
    context.Dispatch2D(width, height);

    s_Progress.samples += constants.SamplesThisFrame;
}

ColorBuffer& PathTracedLighting::GetIndirectRadiance(void)
{
    return s_IndirectRadiance;
}
