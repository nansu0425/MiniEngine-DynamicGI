#include "Model/PathTracer/PathTracer.h"
#include "Model/Raytracing/CameraRays.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Automation/FrameCapture.h"
#include "Core/Camera/Camera.h"
#include "Core/Display/Display.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/ShadowBuffer.h"

#include "Output/Model/CompiledShaders/PathTracerCS.h"
#include "Output/Model/CompiledShaders/PathTracerLambertCS.h"
#include "Output/Model/CompiledShaders/PathTracerIrradianceCS.h"
#include "Output/Model/CompiledShaders/PathTracerLambertIrradianceCS.h"
#include "Output/Model/CompiledShaders/PathTracerProbeHitCS.h"
#include "Output/Model/CompiledShaders/PathTracerProbeHitIrradianceCS.h"

#include <algorithm>

using namespace Math;
using namespace Graphics;

namespace
{
    enum RootParameter { kConstants, kScene, kOutputs = kScene + RaytracingScene::kNumRootParameters, kShadowMap, kNumRootParameters };
    RootSignature s_RootSignature;
    // [PathTracer::Output][PathTracer::Material] 순서다.
    ComputePSO s_PSOs[][3] = {
        { ComputePSO(L"Path Tracer glTF"), ComputePSO(L"Path Tracer Lambert"), ComputePSO(L"Path Tracer DDGI Probe Hit") },
        { ComputePSO(L"Path Tracer Indirect Irradiance glTF"), ComputePSO(L"Path Tracer Indirect Irradiance Lambert"),
            ComputePSO(L"Path Tracer Indirect Irradiance DDGI Probe Hit") },
    };

    // PathTracerCS.hlsl의 u register 순서다.
    enum UAV { kAccumulationUAV, kSceneColorUAV, kNumUAVs };
    SceneHeapTable s_UAVs;
    // PathTracerCS.hlsl의 ShadowMap
    SceneHeapTable s_ShadowMapSRV;
    ColorBuffer s_Accumulation;

    // 누적한 image를 정하는 조건
    struct ImageConditions
    {
        // camera의 위치, 방향, FOV와 scene의 해상도가 정한다.
        CameraRays rays;
        PathTracer::Settings settings;
        Vector3 lightDirection;
        Vector3 lightIlluminance;
        Vector3 skyRadiance;
        // Material DDGI Probe Hit가 읽는 shadow map을 그린 shadow camera. model은 바뀌지 않으므로 shadow camera가 shadow map을 정한다.
        Matrix4 shadowMatrix;
    };

    // Settings의 samplesPerFrame과 targetSamples는 쌓는 속도와 양만 정하므로 비교하지 않는다.
    bool HaveSameImage(const PathTracer::Settings& a, const PathTracer::Settings& b)
    {
        return a.output == b.output && a.material == b.material && a.seed == b.seed && a.maxBounces == b.maxBounces &&
            a.russianRoulette == b.russianRoulette && a.geometricNormals == b.geometricNormals && a.furnace == b.furnace &&
            a.furnaceAlbedo == b.furnaceAlbedo && a.furnaceEmission == b.furnaceEmission;
    }

    // rasterSampleOffset은 비교하지 않는다. 그 값은 TAA jitter를 따라 frame마다 바뀌지만, PT는 pixel 격자 안의 위치를 스스로 고르므로
    // image가 그대로다. 비교하면 TAA를 켠 동안 frame마다 reset되어 sample이 쌓이지 않는다.
    bool HaveSameImage(const ImageConditions& a, const ImageConditions& b)
    {
        return XMVector3Equal(a.rays.origin, b.rays.origin) &&
            XMVector3Equal(a.rays.topLeftCornerDirection, b.rays.topLeftCornerDirection) &&
            XMVector3Equal(a.rays.pixelStepRight, b.rays.pixelStepRight) &&
            XMVector3Equal(a.rays.pixelStepDown, b.rays.pixelStepDown) &&
            XMVector3Equal(a.lightDirection, b.lightDirection) && XMVector3Equal(a.lightIlluminance, b.lightIlluminance) &&
            XMVector3Equal(a.skyRadiance, b.skyRadiance) && XMVector4Equal(a.shadowMatrix.GetX(), b.shadowMatrix.GetX()) &&
            XMVector4Equal(a.shadowMatrix.GetY(), b.shadowMatrix.GetY()) && XMVector4Equal(a.shadowMatrix.GetZ(), b.shadowMatrix.GetZ()) &&
            XMVector4Equal(a.shadowMatrix.GetW(), b.shadowMatrix.GetW()) && HaveSameImage(a.settings, b.settings);
    }

    bool s_ResetRequested = true;
    ImageConditions s_Accumulated;
    PathTracer::Progress s_Progress = {};

    __declspec(align(16)) struct Constants
    {
        CameraRays Rays;
        uint32_t Seed;
        uint32_t FirstSample;
        uint32_t NumSamples;
        uint32_t MaxBounces;
        uint32_t RussianRoulette;
        uint32_t GeometricNormals;
        uint32_t Furnace;
        float FurnaceAlbedo;
        float FurnaceEmission;
        // HLSL은 float3를 앞의 scalar가 남긴 자리에 16 byte 경계를 넘지 않는 한 채운다. 아래의 Vector3처럼 float3가 16 byte 경계에서
        // 시작하도록 그 자리를 채운다.
        float Padding;
        // HLSL의 cbuffer처럼 float3가 16 byte 경계를 넘지 않도록 Vector3로 둔다. Vector3는 16 byte를 차지하므로 뒤에 scalar를 두지 않는다.
        Vector3 LightDirection;
        Vector3 LightIlluminance;
        Vector3 SkyRadiance;
        // raster의 GlobalConstants처럼 Matrix4를 그대로 담고, HLSL은 mul(ShadowMatrix, position)으로 쓴다.
        Matrix4 ShadowMatrix;
    };

    // SceneHeapTable.h의 규칙대로, 앞 frame의 dispatch가 읽고 있을 수 있는 buffer는 GPU를 기다린 뒤에 다시 만든다.
    void FitAccumulationTo(const ColorBuffer& sceneColor)
    {
        if (s_Accumulation.GetWidth() == sceneColor.GetWidth() && s_Accumulation.GetHeight() == sceneColor.GetHeight())
            return;

        g_CommandManager.IdleGPU();
        s_Accumulation.Create(L"Path Tracer Accumulation", sceneColor.GetWidth(), sceneColor.GetHeight(), 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
        s_ResetRequested = true;
    }
}

void PathTracer::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters, 1);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_RootSignature, kScene);
    s_RootSignature[kOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, kNumUAVs);
    s_RootSignature[kShadowMap].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    // PathTracerCS.hlsl의 ShadowMapSampler는 raster의 Renderer와 같은 shadow map 비교 sampler다.
    s_RootSignature.InitStaticSampler(0, SamplerShadowDesc);
    s_RootSignature.Finalize(L"Path Tracer");

    ComputePSO* radiance = s_PSOs[(int)Output::kRadiance];
    radiance[(int)Material::kGltf].SetComputeShader(g_pPathTracerCS, sizeof(g_pPathTracerCS));
    radiance[(int)Material::kLambert].SetComputeShader(g_pPathTracerLambertCS, sizeof(g_pPathTracerLambertCS));
    radiance[(int)Material::kProbeHit].SetComputeShader(g_pPathTracerProbeHitCS, sizeof(g_pPathTracerProbeHitCS));
    ComputePSO* irradiance = s_PSOs[(int)Output::kIndirectIrradiance];
    irradiance[(int)Material::kGltf].SetComputeShader(g_pPathTracerIrradianceCS, sizeof(g_pPathTracerIrradianceCS));
    irradiance[(int)Material::kLambert].SetComputeShader(g_pPathTracerLambertIrradianceCS, sizeof(g_pPathTracerLambertIrradianceCS));
    irradiance[(int)Material::kProbeHit].SetComputeShader(g_pPathTracerProbeHitIrradianceCS, sizeof(g_pPathTracerProbeHitIrradianceCS));
    for (ComputePSO (&materials)[3] : s_PSOs)
    {
        for (ComputePSO& pso : materials)
        {
            pso.SetRootSignature(s_RootSignature);
            pso.Finalize();
        }
    }

    s_UAVs.Create(kNumUAVs);
    s_ShadowMapSRV.Create(1);
}

void PathTracer::Shutdown(void)
{
    s_Accumulation.Destroy();
}

void PathTracer::Reset(void)
{
    s_ResetRequested = true;
}

PathTracer::Progress PathTracer::GetProgress(void)
{
    return s_Progress;
}

void PathTracer::Render(ComputeContext& context, const Settings& settings, const Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, const Vector3& lightDirection, const Vector3& lightIlluminance, const Vector3& skyRadiance,
    ShadowBuffer& shadowMap, const Matrix4& shadowMatrix, ColorBuffer& sceneColor)
{
    ScopedTimer _prof(L"Path Tracer", context);

    FitAccumulationTo(sceneColor);

    // 변경을 알려 주는 곳이 없으므로 쌓던 때의 조건과 견준다. Show를 끈 동안 바뀐 조건도 다시 켤 때 잡힌다.
    const ImageConditions conditions = { DescribeCameraRays(camera, rasterViewport), settings, lightDirection, lightIlluminance, skyRadiance,
        shadowMatrix };
    if (s_ResetRequested || !HaveSameImage(conditions, s_Accumulated))
    {
        s_Accumulated = conditions;
        s_Progress.samples = 0;
        s_Progress.seconds = 0.0;
        ++s_Progress.resets;
        s_ResetRequested = false;
    }

    Constants constants;
    constants.Rays = conditions.rays;
    constants.Seed = settings.seed;
    constants.FirstSample = s_Progress.samples;
    constants.NumSamples = s_Progress.samples < settings.targetSamples ? std::min(settings.samplesPerFrame, settings.targetSamples - s_Progress.samples) : 0;
    constants.MaxBounces = settings.maxBounces;
    constants.RussianRoulette = settings.russianRoulette ? 1 : 0;
    constants.GeometricNormals = settings.geometricNormals ? 1 : 0;
    constants.Furnace = settings.furnace ? 1 : 0;
    constants.FurnaceAlbedo = settings.furnaceAlbedo;
    constants.FurnaceEmission = settings.furnaceEmission;
    constants.LightDirection = lightDirection;
    constants.LightIlluminance = lightIlluminance;
    constants.SkyRadiance = skyRadiance;
    constants.ShadowMatrix = shadowMatrix;

    s_UAVs.PointTo(kAccumulationUAV, s_Accumulation, s_Accumulation.GetUAV());
    s_UAVs.PointTo(kSceneColorUAV, sceneColor, sceneColor.GetUAV());
    s_ShadowMapSRV.PointTo(0, shadowMap, shadowMap.GetSRV());

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSOs[(int)settings.output][(int)settings.material]);
    context.TransitionResource(s_Accumulation, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.TransitionResource(sceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.TransitionResource(shadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    scene.Bind(context, kScene);
    s_UAVs.Bind(context, kOutputs);
    s_ShadowMapSRV.Bind(context, kShadowMap);
    context.Dispatch2D(sceneColor.GetWidth(), sceneColor.GetHeight());

    if (constants.NumSamples != 0)
    {
        s_Progress.samples += constants.NumSamples;
        s_Progress.seconds += GetMeasuredFrameTime();
    }
}

void PathTracer::Capture(ComputeContext& context)
{
    FrameCapture::Capture(FrameCapture::kPathTracer, context, s_Accumulation);
}
