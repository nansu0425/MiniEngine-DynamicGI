#include "Model/DynamicGI/ProbeVisualization.h"
#include "Core/Camera/Camera.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"

#include "Output/Model/CompiledShaders/ProbeSphereVS.h"
#include "Output/Model/CompiledShaders/ProbeSpherePS.h"
#include "Output/Model/CompiledShaders/ProbeDistanceLineVS.h"
#include "Output/Model/CompiledShaders/ProbeDistanceLinePS.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>

using namespace Math;
using namespace Graphics;

namespace
{
    constexpr uint32_t kVerticesPerQuad = 6;
    constexpr uint32_t kVerticesPerLine = 2;
    constexpr uint32_t kTexelsPerProbe = 64;

    enum RootParameter { kConstants, kIrradiance, kNumRootParameters };
    enum LineRootParameter { kLineConstants, kLineInputs, kNumLineRootParameters };
    enum class DistanceLines : uint32_t { kTexelMeans, kRaySamples };

    RootSignature s_RootSignature;
    GraphicsPSO s_PSO(L"Probe Spheres");
    RootSignature s_LineRootSignature;
    GraphicsPSO s_LinePSO(L"Probe Distance Lines");

    __declspec(align(16)) struct Constants
    {
        Matrix4 viewProj;
        IrradianceProbeGrid grid;
        XMFLOAT3 cameraPosition;
        float radius;
    };

    __declspec(align(16)) struct LineConstants
    {
        Matrix4 viewProj;
        IrradianceProbeGrid grid;
        Matrix4 rayRotation;
        uint32_t raysPerProbe;
        uint32_t probe;
        DistanceLines lines;
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

    s_LineRootSignature.Reset(kNumLineRootParameters, 0);
    s_LineRootSignature[kLineConstants].InitAsConstantBuffer(0);
    s_LineRootSignature[kLineInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2, D3D12_SHADER_VISIBILITY_VERTEX);
    s_LineRootSignature.Finalize(L"Probe Distance Lines");

    s_LinePSO.SetRootSignature(s_LineRootSignature);
    s_LinePSO.SetRasterizerState(RasterizerTwoSided);
    s_LinePSO.SetBlendState(BlendDisable);
    s_LinePSO.SetDepthStencilState(DepthStateReadOnly);
    s_LinePSO.SetInputLayout(0, nullptr);
    s_LinePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
    s_LinePSO.SetRenderTargetFormats(1, &colorFormat, g_SceneDepthBuffer.GetFormat());
    s_LinePSO.SetVertexShader(g_pProbeDistanceLineVS, sizeof(g_pProbeDistanceLineVS));
    s_LinePSO.SetPixelShader(g_pProbeDistanceLinePS, sizeof(g_pProbeDistanceLinePS));
    s_LinePSO.Finalize();
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
    context.DrawInstanced(kVerticesPerQuad, GetProbeCount(grid));
}

std::optional<uint32_t> ProbeVisualization::FindProbeUnderCursor(const Camera& camera, const IrradianceProbeGrid& grid, float radiusPerSpacing)
{
    const ImVec2 cursor = ImGui::GetMousePos();
    const float radius = radiusPerSpacing * GetShortestSpacing(grid);
    std::optional<uint32_t> nearest;
    float nearestDistance = FLT_MAX;
    for (uint32_t probe = 0; probe < GetProbeCount(grid); ++probe)
    {
        const Vector3 position = GetProbePosition(grid, probe);
        ImVec2 center;
        if (!SceneEditor::ProjectToScene(camera, position, center))
            continue;

        const float radiusInPixels = radius / SceneEditor::GetWorldLengthPerPixel(camera, position);
        const float dx = center.x - cursor.x;
        const float dy = center.y - cursor.y;
        const float distance = Length(position - camera.GetPosition());
        if (dx * dx + dy * dy <= radiusInPixels * radiusInPixels && distance < nearestDistance)
        {
            nearest = probe;
            nearestDistance = distance;
        }
    }
    return nearest;
}

void ProbeVisualization::DrawDistanceLines(GraphicsContext& context, const Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
    ColorBuffer& probesDistance, ColorBuffer& raySamples, const Matrix4& raySamplesRotation, const IrradianceProbeGrid& grid, uint32_t raysPerProbe,
    uint32_t probe, const Settings& settings)
{
    ScopedTimer _prof(L"Probe Distance Lines", context);

    LineConstants constants;
    constants.viewProj = camera.GetViewProjMatrix();
    constants.grid = grid;
    constants.rayRotation = raySamplesRotation;
    constants.raysPerProbe = raysPerProbe;
    constants.probe = probe;

    context.SetRootSignature(s_LineRootSignature);
    context.SetPipelineState(s_LinePSO);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    context.TransitionResource(probesDistance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(raySamples, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    context.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
    context.SetViewportAndScissor(viewport, scissor);
    context.SetDynamicDescriptor(kLineInputs, 0, probesDistance.GetSRV());
    context.SetDynamicDescriptor(kLineInputs, 1, raySamples.GetSRV());

    if (settings.showTexelMeans)
    {
        constants.lines = DistanceLines::kTexelMeans;
        context.SetDynamicConstantBufferView(kLineConstants, sizeof(constants), &constants);
        context.DrawInstanced(kVerticesPerLine, kTexelsPerProbe);
    }
    if (settings.showRaySamples)
    {
        constants.lines = DistanceLines::kRaySamples;
        context.SetDynamicConstantBufferView(kLineConstants, sizeof(constants), &constants);
        context.DrawInstanced(kVerticesPerLine, raysPerProbe);
    }
}
