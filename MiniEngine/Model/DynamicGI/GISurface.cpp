#include "Model/DynamicGI/GISurface.h"
#include "Model/Raytracing/CameraRays.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Resource/GpuBuffer.h"

#include "Output/Model/CompiledShaders/GISurfaceCS.h"

using namespace Graphics;

namespace
{
    // GISurface.hlsli의 GISurface가 StructuredBuffer에서 차지하는 크기
    constexpr uint32_t kGISurfaceStride = 72;

    enum RootBindings { kConstants, kScene, kSurfaces = kScene + RaytracingScene::kNumRootParameters, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"GI Surface");

    StructuredBuffer s_Surfaces;

    __declspec(align(16)) struct Constants
    {
        CameraRays Rays;
        uint32_t Width;
        uint32_t Height;
    };

    void FitSurfacesTo(uint32_t width, uint32_t height)
    {
        if (s_Surfaces.GetElementCount() == width * height)
            return;

        g_CommandManager.IdleGPU();
        s_Surfaces.Create(L"GI Surfaces", width * height, kGISurfaceStride);
    }
}

void GISurface::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters, 0);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_RootSignature, kScene);
    s_RootSignature[kSurfaces].InitAsBufferUAV(0);
    s_RootSignature.Finalize(L"GI Surface");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pGISurfaceCS, sizeof(g_pGISurfaceCS));
    s_PSO.Finalize();
}

void GISurface::Shutdown(void)
{
    s_Surfaces.Destroy();
}

void GISurface::Update(ComputeContext& context, const GIFrameInputs& inputs, uint32_t width, uint32_t height)
{
    ScopedTimer _prof(L"GI Surface", context);

    FitSurfacesTo(width, height);

    Constants constants;
    constants.Rays = DescribeCameraRays(inputs.camera, inputs.rasterViewport);
    constants.Width = width;
    constants.Height = height;

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(s_Surfaces, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    inputs.scene.Bind(context, kScene);
    context.SetBufferUAV(kSurfaces, s_Surfaces);
    context.Dispatch2D(width, height);
}

StructuredBuffer& GISurface::GetSurfaces(void)
{
    return s_Surfaces;
}
