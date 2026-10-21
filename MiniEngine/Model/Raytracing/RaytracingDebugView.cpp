#include "Model/Raytracing/RaytracingDebugView.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Renderer/Renderer.h"
#include "Core/Camera/Camera.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Descriptor/DescriptorHeap.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/UI/EngineTuning.h"

#include "Output/Model/CompiledShaders/RaytracingDebugViewCS.h"

using namespace Math;
using namespace Graphics;

namespace
{
    // RaytracingDebugViewCS.hlsl의 View와 같은 값이다.
    enum View
    {
        kOff, kHitT, kInstance, kFrontFace,
        kGeometryNormal, kShadingNormal, kUV0, kBaseColor, kMetallic, kRoughness, kEmissive,
        kNumViews
    };
    const char* kViewLabels[] =
    {
        "Off", "Hit T", "Instance", "Front Face",
        "Geometry Normal", "Shading Normal", "UV0", "Base Color", "Metallic", "Roughness", "Emissive"
    };
    EnumVar s_View("Graphics/Raytracing/Debug View", kOff, kNumViews, kViewLabels);

    enum RootParameter { kConstants, kScene, kOutput = kScene + RaytracingScene::kNumRootParameters, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Raytracing Debug View");

    // RaytracingScene::Bind가 scene descriptor heap을 bind하므로 output UAV도 그 heap에 둔다.
    DescriptorHandle s_OutputUAV;
    uint32_t s_OutputVersionID = UINT32_MAX;

    __declspec(align(16)) struct Constants
    {
        XMFLOAT3 CameraPosition;
        uint32_t View;
        XMFLOAT3 TopLeftRayDirection;
        float NearZ;
        XMFLOAT3 PixelStepRight;
        float FarZ;
        XMFLOAT3 PixelStepDown;
    };

    // raster와 같은 시야가 되도록 camera의 FOV로 화면의 각 pixel을 지나는 ray 방향을 만든다.
    void DescribeCameraRays(const Camera& camera, const ColorBuffer& target, Constants& constants)
    {
        const float width = (float)target.GetWidth();
        const float height = (float)target.GetHeight();
        const float halfScreenHeight = tanf(camera.GetFOV() * 0.5f);
        const float halfScreenWidth = halfScreenHeight * width / height;
        const Vector3 right = camera.GetRightVec();
        const Vector3 up = camera.GetUpVec();

        XMStoreFloat3(&constants.CameraPosition, camera.GetPosition());
        XMStoreFloat3(&constants.TopLeftRayDirection, camera.GetForwardVec() - right * halfScreenWidth + up * halfScreenHeight);
        XMStoreFloat3(&constants.PixelStepRight, right * (2.0f * halfScreenWidth / width));
        XMStoreFloat3(&constants.PixelStepDown, up * (-2.0f * halfScreenHeight / height));
    }

    // target은 GPU가 멈춘 동안에만 다시 만들어지므로, 그때 heap의 descriptor를 바로 덮어써도 된다.
    void UpdateOutputUAV(const ColorBuffer& target)
    {
        if (s_OutputVersionID == target.GetVersionID())
            return;

        g_Device->CopyDescriptorsSimple(1, s_OutputUAV, target.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        s_OutputVersionID = target.GetVersionID();
    }
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

    s_OutputUAV = Renderer::s_TextureHeap.Alloc(1);
}

void RaytracingDebugView::Render(ComputeContext& context, const Camera& camera, const RaytracingScene& scene, ColorBuffer& target)
{
    if (s_View == kOff)
        return;

    ScopedTimer _prof(L"Raytracing Debug View", context);

    Constants constants;
    DescribeCameraRays(camera, target, constants);
    constants.View = s_View;
    constants.NearZ = camera.GetNearClip();
    constants.FarZ = camera.GetFarClip();

    UpdateOutputUAV(target);

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    scene.Bind(context, kScene);
    context.SetDescriptorTable(kOutput, s_OutputUAV);
    context.Dispatch2D(target.GetWidth(), target.GetHeight());
}
