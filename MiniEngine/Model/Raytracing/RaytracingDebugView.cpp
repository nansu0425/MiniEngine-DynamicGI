#include "Model/Raytracing/RaytracingDebugView.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Core/Camera/Camera.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/UI/EngineTuning.h"

#include "Output/Model/CompiledShaders/RaytracingDebugViewCS.h"

using namespace Math;

namespace
{
    // RaytracingDebugViewCS.hlsl의 View와 같은 값이다.
    enum View { kOff, kHitT, kInstance, kFrontFace, kNumViews };
    const char* kViewLabels[] = { "Off", "Hit T", "Instance", "Front Face" };
    EnumVar s_View("Graphics/Raytracing/Debug View", kOff, kNumViews, kViewLabels);

    enum RootParameter { kConstants, kScene, kOutput, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Raytracing Debug View");

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
}

void RaytracingDebugView::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    s_RootSignature[kScene].InitAsBufferSRV(0);
    s_RootSignature[kOutput].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature.Finalize(L"Raytracing Debug View");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pRaytracingDebugViewCS, sizeof(g_pRaytracingDebugViewCS));
    s_PSO.Finalize();
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

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    context.SetAccelerationStructure(kScene, scene.GetTLAS());
    context.SetDynamicDescriptor(kOutput, 0, target.GetUAV());
    context.Dispatch2D(target.GetWidth(), target.GetHeight());
}
