#include "Core/pch.h"
#include "Model/DynamicGI/DynamicGIVolumeBox.h"
#include "Core/Camera/Camera.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/BufferManager.h"

#include "Output/Model/CompiledShaders/DynamicGIVolumeEdgesVS.h"
#include "Output/Model/CompiledShaders/DynamicGIVolumeFacesVS.h"
#include "Output/Model/CompiledShaders/DynamicGIVolumeBoxPS.h"

using namespace Math;
using namespace Graphics;

namespace
{
    constexpr uint32_t kNumEdgeVertices = 24;
    constexpr uint32_t kNumFaceVertices = 36;
    constexpr float kFaceOpacity = 0.1f;
    constexpr float kVisibleEdgeOpacity = 1.0f;
    constexpr float kHiddenEdgeOpacity = 0.3f;
    const XMFLOAT3 kColor(0.25f, 0.78f, 1.0f);

    RootSignature s_RootSignature;
    GraphicsPSO s_FacesPSO(L"Dynamic GI Volume Faces");
    GraphicsPSO s_VisibleEdgesPSO(L"Dynamic GI Volume Visible Edges");
    GraphicsPSO s_HiddenEdgesPSO(L"Dynamic GI Volume Hidden Edges");

    __declspec(align(16)) struct Constants
    {
        Matrix4 viewProj;
        XMFLOAT3 boundsMin;
        float opacity;
        XMFLOAT3 boundsMax;
        float padding;
        XMFLOAT3 color;
    };

    void InitializePSO(GraphicsPSO& pso, const D3D12_DEPTH_STENCIL_DESC& depth, D3D12_PRIMITIVE_TOPOLOGY_TYPE topology,
        const void* vertexShader, size_t vertexShaderSize)
    {
        pso.SetRootSignature(s_RootSignature);
        pso.SetRasterizerState(RasterizerTwoSided);
        pso.SetBlendState(BlendPreMultiplied);
        pso.SetDepthStencilState(depth);
        pso.SetInputLayout(0, nullptr);
        pso.SetPrimitiveTopologyType(topology);
        const DXGI_FORMAT overlayFormat = g_SceneOverlayBuffer.GetFormat();
        pso.SetRenderTargetFormats(1, &overlayFormat, g_SceneDepthBuffer.GetFormat());
        pso.SetVertexShader(vertexShader, vertexShaderSize);
        pso.SetPixelShader(g_pDynamicGIVolumeBoxPS, sizeof(g_pDynamicGIVolumeBoxPS));
        pso.Finalize();
    }

    void DrawPass(GraphicsContext& context, GraphicsPSO& pso, D3D_PRIMITIVE_TOPOLOGY topology, uint32_t vertexCount,
        Constants constants, float opacity)
    {
        constants.opacity = opacity;
        context.SetPipelineState(pso);
        context.SetPrimitiveTopology(topology);
        context.SetDynamicConstantBufferView(0, sizeof(constants), &constants);
        context.Draw(vertexCount);
    }
}

void DynamicGIVolumeBox::Initialize(void)
{
    s_RootSignature.Reset(1, 0);
    s_RootSignature[0].InitAsConstantBuffer(0);
    s_RootSignature.Finalize(L"Dynamic GI Volume Box", D3D12_ROOT_SIGNATURE_FLAG_NONE);

    InitializePSO(s_FacesPSO, DepthStateReadOnly, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        g_pDynamicGIVolumeFacesVS, sizeof(g_pDynamicGIVolumeFacesVS));
    InitializePSO(s_VisibleEdgesPSO, DepthStateReadOnly, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
        g_pDynamicGIVolumeEdgesVS, sizeof(g_pDynamicGIVolumeEdgesVS));
    InitializePSO(s_HiddenEdgesPSO, DepthStateReadOnlyReversed, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
        g_pDynamicGIVolumeEdgesVS, sizeof(g_pDynamicGIVolumeEdgesVS));
}

void DynamicGIVolumeBox::Draw(GraphicsContext& context, const Camera& camera, const AxisAlignedBox& bounds)
{
    ScopedTimer _prof(L"Dynamic GI Volume Box", context);

    Constants constants = {};
    constants.viewProj = camera.GetViewProjMatrix();
    XMStoreFloat3(&constants.boundsMin, bounds.GetMin());
    XMStoreFloat3(&constants.boundsMax, bounds.GetMax());
    constants.color = kColor;

    context.SetRootSignature(s_RootSignature);
    context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    context.TransitionResource(g_SceneOverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_SceneOverlayBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
    context.SetViewportAndScissor(0, 0, g_SceneOverlayBuffer.GetWidth(), g_SceneOverlayBuffer.GetHeight());

    DrawPass(context, s_FacesPSO, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, kNumFaceVertices, constants, kFaceOpacity);
    DrawPass(context, s_HiddenEdgesPSO, D3D_PRIMITIVE_TOPOLOGY_LINELIST, kNumEdgeVertices, constants, kHiddenEdgeOpacity);
    DrawPass(context, s_VisibleEdgesPSO, D3D_PRIMITIVE_TOPOLOGY_LINELIST, kNumEdgeVertices, constants, kVisibleEdgeOpacity);
}
