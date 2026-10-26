#include "Model/Raytracing/RaytracingDebugView.h"
#include "Model/Raytracing/CameraRays.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Camera/Camera.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/ColorBuffer.h"

#include "Output/Model/CompiledShaders/RaytracingDebugViewCS.h"

using namespace Math;
using namespace Graphics;

namespace
{
    const char* kViewLabels[] =
    {
        "Hit T", "Instance", "Front Face",
        "Geometry Normal", "Shading Normal", "UV0", "Base Color", "Metallic", "Roughness", "Emissive",
        "Light Visibility", "Light Culling Difference"
    };
    static_assert(_countof(kViewLabels) == RaytracingDebugView::kNumViews, "view label 개수가 View와 다르다");

    enum RootParameter { kConstants, kScene, kOutput = kScene + RaytracingScene::kNumRootParameters, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Raytracing Debug View");

    SceneHeapTable s_OutputUAV;

    __declspec(align(16)) struct Constants
    {
        CameraRays Rays;
        uint32_t View;
        float NearZ;
        float FarZ;
        uint32_t CullSingleSidedBackFaces;
        XMFLOAT3 LightDirection;
    };
}

void RaytracingDebugView::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_RootSignature, kScene);
    s_RootSignature[kOutput].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature.Finalize(L"Raytracing Debug View");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pRaytracingDebugViewCS, sizeof(g_pRaytracingDebugViewCS));
    s_PSO.Finalize();

    s_OutputUAV.Create(1);
}

const char* RaytracingDebugView::GetViewLabel(View view)
{
    return kViewLabels[view];
}

void RaytracingDebugView::Render(ComputeContext& context, View view, bool cullSingleSidedBackFaces, const Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, const Vector3& lightDirection, ColorBuffer& target)
{
    ScopedTimer _prof(L"Raytracing Debug View", context);

    Constants constants;
    constants.Rays = DescribeCameraRays(camera, rasterViewport);
    constants.View = view;
    constants.NearZ = camera.GetNearClip();
    constants.FarZ = camera.GetFarClip();
    constants.CullSingleSidedBackFaces = cullSingleSidedBackFaces;
    XMStoreFloat3(&constants.LightDirection, lightDirection);

    s_OutputUAV.PointTo(0, target, target.GetUAV());

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    scene.Bind(context, kScene);
    s_OutputUAV.Bind(context, kOutput);
    context.Dispatch2D(target.GetWidth(), target.GetHeight());
}
