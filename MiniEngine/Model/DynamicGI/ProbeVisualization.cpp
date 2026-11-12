#include "Model/DynamicGI/ProbeVisualization.h"
#include "Core/Camera/Camera.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"

#include "Output/Model/CompiledShaders/ProbeSphereVS.h"
#include "Output/Model/CompiledShaders/ProbeSpherePS.h"

#include <algorithm>

using namespace Math;
using namespace Graphics;

namespace
{
    constexpr uint32_t kVerticesPerQuad = 6;

    enum RootParameter { kConstants, kIrradiance, kNumRootParameters };

    RootSignature s_RootSignature;
    GraphicsPSO s_PSO(L"Probe Spheres");

    __declspec(align(16)) struct Constants
    {
        Matrix4 viewProj;
        IrradianceProbeGrid grid;
        XMFLOAT3 cameraPosition;
        float radius;
    };

    float GetShortestSpacing(const IrradianceProbeGrid& grid)
    {
        return std::min({ (float)grid.spacing.GetX(), (float)grid.spacing.GetY(), (float)grid.spacing.GetZ() });
    }
}

void ProbeVisualization::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters, 1);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    s_RootSignature[kIrradiance].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    s_RootSignature.InitStaticSampler(0, SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    s_RootSignature.Finalize(L"Probe Spheres");

    const DXGI_FORMAT colorFormat = g_SceneColorBuffer.GetFormat();
    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetRasterizerState(RasterizerTwoSided);
    s_PSO.SetBlendState(BlendDisable);
    s_PSO.SetDepthStencilState(DepthStateReadWrite);
    s_PSO.SetInputLayout(0, nullptr);
    s_PSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    s_PSO.SetRenderTargetFormats(1, &colorFormat, g_SceneDepthBuffer.GetFormat());
    s_PSO.SetVertexShader(g_pProbeSphereVS, sizeof(g_pProbeSphereVS));
    s_PSO.SetPixelShader(g_pProbeSpherePS, sizeof(g_pProbeSpherePS));
    s_PSO.Finalize();
}

void ProbeVisualization::DrawSpheres(GraphicsContext& context, const Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
    ColorBuffer& irradiance, const IrradianceProbeGrid& grid, float radiusPerSpacing)
{
    ScopedTimer _prof(L"Probe Spheres", context);

    Constants constants;
    constants.viewProj = camera.GetViewProjMatrix();
    constants.grid = grid;
    XMStoreFloat3(&constants.cameraPosition, camera.GetPosition());
    constants.radius = radiusPerSpacing * GetShortestSpacing(grid);

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    context.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
    context.SetViewportAndScissor(viewport, scissor);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    context.SetDynamicDescriptor(kIrradiance, 0, irradiance.GetSRV());
    context.DrawInstanced(kVerticesPerQuad, grid.counts[0] * grid.counts[1] * grid.counts[2]);
}
