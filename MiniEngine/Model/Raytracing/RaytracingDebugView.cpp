#include "Model/Raytracing/RaytracingDebugView.h"
#include "Model/Raytracing/CameraRays.h"
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
        kSunVisibility, kSunCullingDifference,
        kNumViews
    };
    const char* kViewLabels[] =
    {
        "Off", "Hit T", "Instance", "Front Face",
        "Geometry Normal", "Shading Normal", "UV0", "Base Color", "Metallic", "Roughness", "Emissive",
        "Sun Visibility", "Sun Culling Difference"
    };
    EnumVar s_View("Graphics/Raytracing/Debug View", kOff, kNumViews, kViewLabels);
    // 끄면 single-sided material의 뒷면도 ray에 맞아서 culling이 ray에 주는 차이를 볼 수 있다.
    BoolVar s_CullSingleSidedBackFaces("Graphics/Raytracing/Cull Single-Sided Back Faces", true);

    enum RootParameter { kConstants, kScene, kOutput = kScene + RaytracingScene::kNumRootParameters, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Raytracing Debug View");

    // RaytracingScene::Bind가 scene descriptor heap을 bind하므로 output UAV도 그 heap에 둔다.
    DescriptorHandle s_OutputUAV;
    uint32_t s_OutputVersionID = UINT32_MAX;

    __declspec(align(16)) struct Constants
    {
        CameraRays Rays;
        uint32_t View;
        float NearZ;
        float FarZ;
        uint32_t CullSingleSidedBackFaces;
        XMFLOAT3 SunDirection;
    };

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

void RaytracingDebugView::Render(ComputeContext& context, const Camera& camera, const D3D12_VIEWPORT& rasterViewport, const RaytracingScene& scene, const Vector3& sunDirection, ColorBuffer& target)
{
    if (s_View == kOff)
        return;

    ScopedTimer _prof(L"Raytracing Debug View", context);

    Constants constants;
    constants.Rays = DescribeCameraRays(camera, rasterViewport);
    constants.View = s_View;
    constants.NearZ = camera.GetNearClip();
    constants.FarZ = camera.GetFarClip();
    constants.CullSingleSidedBackFaces = s_CullSingleSidedBackFaces;
    XMStoreFloat3(&constants.SunDirection, sunDirection);

    UpdateOutputUAV(target);

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    scene.Bind(context, kScene);
    context.SetDescriptorTable(kOutput, s_OutputUAV);
    context.Dispatch2D(target.GetWidth(), target.GetHeight());
}
