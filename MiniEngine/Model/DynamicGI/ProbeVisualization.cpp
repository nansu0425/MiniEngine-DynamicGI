#include "Model/DynamicGI/ProbeVisualization.h"
#include "Model/DynamicGI/ProbeReadback.h"
#include "Core/Camera/Camera.h"
#include "Core/Editor/SceneEditor.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/GpuBuffer.h"
#include "Core/UI/ImGuiLayer.h"

#include "Output/Model/CompiledShaders/ProbeSphereVS.h"
#include "Output/Model/CompiledShaders/ProbeSpherePS.h"
#include "Output/Model/CompiledShaders/ProbeDistanceLineVS.h"
#include "Output/Model/CompiledShaders/ProbeDistanceLinePS.h"
#include "Output/Model/CompiledShaders/ProbeContributionRibbonVS.h"
#include "Output/Model/CompiledShaders/ProbeContributionRibbonPS.h"
#include "Output/Model/CompiledShaders/ProbeOffsetLineVS.h"
#include "Output/Model/CompiledShaders/ProbeOffsetLinePS.h"

#include <imgui.h>
#include <cfloat>

using namespace Math;
using namespace Graphics;

namespace
{
    constexpr uint32_t kVerticesPerQuad = 6;
    constexpr uint32_t kVerticesPerLine = 2;
    constexpr uint32_t kVerticesPerArrow = 9;

    enum RootParameter { kConstants, kInputs, kNumRootParameters };
    enum LineRootParameter { kLineConstants, kLineInputs, kNumLineRootParameters };
    enum class DistanceLines : uint32_t { kTexelMeans, kRaySamples, kTexelDistances };
    enum RibbonRootParameter { kRibbonConstants, kRibbonContribution, kNumRibbonRootParameters };
    enum OffsetLineRootParameter { kOffsetLineConstants, kOffsetLineInputs, kNumOffsetLineRootParameters };

    constexpr uint32_t kNumCellEdges = 12;
    constexpr uint32_t kNumContributionRibbons = ProbeContribution::kNumCorners + kNumCellEdges + 1 + ProbeContribution::kNumCorners + 1;
    constexpr uint32_t kNoHoveredCorner = 0xffffffff;
    constexpr uint32_t kNoHighlightedRay = 0xffffffff;
    constexpr float kNormalLengthPerSpacing = 0.5f;

    RootSignature s_RootSignature;
    GraphicsPSO s_PSO(L"Probe Spheres");
    RootSignature s_LineRootSignature;
    GraphicsPSO s_LinePSO(L"Probe Distance Lines");
    RootSignature s_RibbonRootSignature;
    GraphicsPSO s_RibbonPSO(L"Probe Contribution Ribbons");
    RootSignature s_OffsetLineRootSignature;
    GraphicsPSO s_OffsetLinePSO(L"Probe Offset Lines");

    __declspec(align(16)) struct Constants
    {
        Matrix4 viewProj;
        IrradianceProbeGrid grid;
        XMFLOAT3 cameraPosition;
        float radius;
        uint32_t classification;
        float maxBackFaceRatio;
    };

    __declspec(align(16)) struct LineConstants
    {
        Matrix4 viewProj;
        IrradianceProbeGrid grid;
        Matrix4 rayRotation;
        uint32_t raysPerProbe;
        uint32_t probe;
        DistanceLines lines;
        uint32_t distanceTexelsPerSide;
        uint32_t highlightedRay;
    };

    __declspec(align(16)) struct RibbonConstants
    {
        Matrix4 viewProj;
        IrradianceProbeGrid grid;
        XMFLOAT2 viewportSize;
        float nearClip;
        float normalLength;
        uint32_t hoveredCorner;
    };

    __declspec(align(16)) struct OffsetLineConstants
    {
        Matrix4 viewProj;
        IrradianceProbeGrid grid;
    };
}

void ProbeVisualization::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters, 1);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    s_RootSignature[kInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 3);
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
    s_LineRootSignature[kLineInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4, D3D12_SHADER_VISIBILITY_VERTEX);
    s_LineRootSignature.Finalize(L"Probe Distance Lines");

    s_LinePSO.SetRootSignature(s_LineRootSignature);
    s_LinePSO.SetRasterizerState(RasterizerTwoSided);
    s_LinePSO.SetBlendState(BlendPreMultiplied);
    s_LinePSO.SetDepthStencilState(DepthStateReadOnly);
    s_LinePSO.SetInputLayout(0, nullptr);
    s_LinePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
    const DXGI_FORMAT overlayFormat = g_SceneOverlayBuffer.GetFormat();
    s_LinePSO.SetRenderTargetFormats(1, &overlayFormat, g_SceneDepthBuffer.GetFormat());
    s_LinePSO.SetVertexShader(g_pProbeDistanceLineVS, sizeof(g_pProbeDistanceLineVS));
    s_LinePSO.SetPixelShader(g_pProbeDistanceLinePS, sizeof(g_pProbeDistanceLinePS));
    s_LinePSO.Finalize();

    s_RibbonRootSignature.Reset(kNumRibbonRootParameters, 0);
    s_RibbonRootSignature[kRibbonConstants].InitAsConstantBuffer(0);
    s_RibbonRootSignature[kRibbonContribution].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_VERTEX);
    s_RibbonRootSignature.Finalize(L"Probe Contribution Ribbons");

    s_RibbonPSO.SetRootSignature(s_RibbonRootSignature);
    s_RibbonPSO.SetRasterizerState(RasterizerTwoSided);
    s_RibbonPSO.SetBlendState(BlendDisable);
    s_RibbonPSO.SetDepthStencilState(DepthStateDisabled);
    s_RibbonPSO.SetInputLayout(0, nullptr);
    s_RibbonPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    s_RibbonPSO.SetRenderTargetFormat(g_SceneOverlayBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
    s_RibbonPSO.SetVertexShader(g_pProbeContributionRibbonVS, sizeof(g_pProbeContributionRibbonVS));
    s_RibbonPSO.SetPixelShader(g_pProbeContributionRibbonPS, sizeof(g_pProbeContributionRibbonPS));
    s_RibbonPSO.Finalize();

    s_OffsetLineRootSignature.Reset(kNumOffsetLineRootParameters, 0);
    s_OffsetLineRootSignature[kOffsetLineConstants].InitAsConstantBuffer(0);
    s_OffsetLineRootSignature[kOffsetLineInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_VERTEX);
    s_OffsetLineRootSignature.Finalize(L"Probe Offset Lines");

    s_OffsetLinePSO.SetRootSignature(s_OffsetLineRootSignature);
    s_OffsetLinePSO.SetRasterizerState(RasterizerTwoSided);
    s_OffsetLinePSO.SetBlendState(BlendDisable);
    s_OffsetLinePSO.SetDepthStencilState(DepthStateReadOnly);
    s_OffsetLinePSO.SetInputLayout(0, nullptr);
    s_OffsetLinePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
    s_OffsetLinePSO.SetRenderTargetFormats(1, &overlayFormat, g_SceneDepthBuffer.GetFormat());
    s_OffsetLinePSO.SetVertexShader(g_pProbeOffsetLineVS, sizeof(g_pProbeOffsetLineVS));
    s_OffsetLinePSO.SetPixelShader(g_pProbeOffsetLinePS, sizeof(g_pProbeOffsetLinePS));
    s_OffsetLinePSO.Finalize();
}

void ProbeVisualization::DrawSpheres(GraphicsContext& context, const Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
    const ProbeLighting::Probes& probes, const ProbeWeighting::Settings& weighting, float radiusPerSpacing)
{
    ScopedTimer _prof(L"Probe Spheres", context);

    Constants constants;
    constants.viewProj = camera.GetViewProjMatrix();
    constants.grid = probes.grid;
    XMStoreFloat3(&constants.cameraPosition, camera.GetPosition());
    constants.radius = radiusPerSpacing * GetShortestProbeSpacing(probes.grid);
    constants.classification = weighting.classification ? 1u : 0u;
    constants.maxBackFaceRatio = weighting.maxBackFaceRatio;

    context.SetRootSignature(s_RootSignature);
    context.SetPipelineState(s_PSO);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    context.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
    context.SetViewportAndScissor(viewport, scissor);
    context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
    context.TransitionResource(probes.offset, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.SetDynamicDescriptor(kInputs, 0, probes.irradiance.GetSRV());
    context.SetDynamicDescriptor(kInputs, 1, probes.backFaceRatio.GetSRV());
    context.SetDynamicDescriptor(kInputs, 2, probes.offset.GetSRV());
    context.DrawInstanced(kVerticesPerQuad, GetProbeCount(probes.grid));
}

std::optional<uint32_t> ProbeVisualization::FindProbeUnderCursor(const Camera& camera, const IrradianceProbeGrid& grid, float radiusPerSpacing)
{
    const ImVec2 cursor = ImGui::GetMousePos();
    const float radius = radiusPerSpacing * GetShortestProbeSpacing(grid);
    std::optional<uint32_t> nearest;
    float nearestDistance = FLT_MAX;
    for (uint32_t probe = 0; probe < GetProbeCount(grid); ++probe)
    {
        const Vector3 position = ProbeReadback::GetProbePosition(grid, probe);
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

void ProbeVisualization::DrawDistanceLines(GraphicsContext& context, const Camera& camera, const ProbeLighting::Probes& probes, uint32_t probe,
    const Settings& settings, std::optional<uint32_t> highlightedRay)
{
    ScopedTimer _prof(L"Probe Distance Lines", context);

    LineConstants constants;
    constants.viewProj = camera.GetViewProjMatrix();
    constants.grid = probes.grid;
    constants.rayRotation = probes.raySamplesRotation;
    constants.raysPerProbe = probes.raysPerProbe;
    constants.probe = probe;
    constants.distanceTexelsPerSide = probes.distanceTexelsPerSide;
    constants.highlightedRay = highlightedRay.value_or(kNoHighlightedRay);

    context.SetRootSignature(s_LineRootSignature);
    context.SetPipelineState(s_LinePSO);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    context.TransitionResource(probes.distance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(probes.raySamples, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(probes.texelDistance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(probes.offset, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    context.TransitionResource(g_SceneOverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_SceneOverlayBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
    context.SetViewportAndScissor(0, 0, g_SceneOverlayBuffer.GetWidth(), g_SceneOverlayBuffer.GetHeight());
    context.SetDynamicDescriptor(kLineInputs, 0, probes.distance.GetSRV());
    context.SetDynamicDescriptor(kLineInputs, 1, probes.raySamples.GetSRV());
    context.SetDynamicDescriptor(kLineInputs, 2, probes.texelDistance.GetSRV());
    context.SetDynamicDescriptor(kLineInputs, 3, probes.offset.GetSRV());

    if (settings.showTexelMeans)
    {
        constants.lines = DistanceLines::kTexelMeans;
        context.SetDynamicConstantBufferView(kLineConstants, sizeof(constants), &constants);
        context.DrawInstanced(kVerticesPerLine, probes.distanceTexelsPerSide * probes.distanceTexelsPerSide);
    }
    if (settings.showTexelDistances)
    {
        constants.lines = DistanceLines::kTexelDistances;
        context.SetDynamicConstantBufferView(kLineConstants, sizeof(constants), &constants);
        context.DrawInstanced(kVerticesPerLine, probes.distanceTexelsPerSide * probes.distanceTexelsPerSide);
    }
    if (settings.showRaySamples)
    {
        constants.lines = DistanceLines::kRaySamples;
        context.SetDynamicConstantBufferView(kLineConstants, sizeof(constants), &constants);
        context.DrawInstanced(kVerticesPerLine, probes.raysPerProbe);
    }
}

void ProbeVisualization::DrawOffsetLines(GraphicsContext& context, const Camera& camera, const ProbeLighting::Probes& probes)
{
    ScopedTimer _prof(L"Probe Offset Lines", context);

    const OffsetLineConstants constants = { .viewProj = camera.GetViewProjMatrix(), .grid = probes.grid };
    context.SetRootSignature(s_OffsetLineRootSignature);
    context.SetPipelineState(s_OffsetLinePSO);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    context.TransitionResource(probes.offset, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    context.TransitionResource(g_SceneOverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_SceneOverlayBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
    context.SetViewportAndScissor(0, 0, g_SceneOverlayBuffer.GetWidth(), g_SceneOverlayBuffer.GetHeight());
    context.SetDynamicConstantBufferView(kOffsetLineConstants, sizeof(constants), &constants);
    context.SetDynamicDescriptor(kOffsetLineInputs, 0, probes.offset.GetSRV());
    context.DrawInstanced(kVerticesPerLine, GetProbeCount(probes.grid));
}

void ProbeVisualization::DrawContribution(GraphicsContext& context, const Camera& camera, const IrradianceProbeGrid& grid, StructuredBuffer& contribution,
    std::optional<uint32_t> hoveredCorner)
{
    ScopedTimer _prof(L"Probe Contribution Ribbons", context);

    RibbonConstants constants;
    constants.viewProj = camera.GetViewProjMatrix();
    constants.grid = grid;
    const uint32_t width = g_SceneOverlayBuffer.GetWidth();
    const uint32_t height = g_SceneOverlayBuffer.GetHeight();
    constants.viewportSize = XMFLOAT2((float)width, (float)height);
    constants.nearClip = camera.GetNearClip();
    constants.normalLength = kNormalLengthPerSpacing * GetShortestProbeSpacing(grid);
    constants.hoveredCorner = hoveredCorner.value_or(kNoHoveredCorner);

    context.SetRootSignature(s_RibbonRootSignature);
    context.SetPipelineState(s_RibbonPSO);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.TransitionResource(contribution, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(g_SceneOverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_SceneOverlayBuffer.GetRTV());
    context.SetViewportAndScissor(0, 0, width, height);
    context.SetDynamicConstantBufferView(kRibbonConstants, sizeof(constants), &constants);
    context.SetDynamicDescriptor(kRibbonContribution, 0, contribution.GetSRV());
    context.DrawInstanced(kVerticesPerArrow, kNumContributionRibbons);
}

void ProbeVisualization::AnnotateContribution(const Camera& camera, const ProbeContribution::Surface& contribution,
    const ContributionLabels& labels)
{
    const ImU32 textColor = IM_COL32(255, 255, 255, 255);
    const ImU32 backgroundColor = IM_COL32(0, 0, 0, 160);
    const ImU32 hoveredBorderColor = IM_COL32(255, 140, 26, 255);
    const float weightSize = ImGui::GetFontSize() * labels.scale;
    const float letterSize = weightSize * 1.5f;
    const ImVec2 padding(4.0f * labels.scale, 2.0f * labels.scale);

    const RECT scene = ImGuiLayer::GetSceneViewport();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->PushClipRect(ImVec2((float)scene.left, (float)scene.top), ImVec2((float)scene.right, (float)scene.bottom));
    for (uint32_t corner = 0; corner < ProbeContribution::kNumCorners; ++corner)
    {
        const ProbeContribution::Corner& probe = contribution.corners[corner];
        ImVec2 center;
        if (!SceneEditor::ProjectToScene(camera, Vector3(probe.position.x, probe.position.y, probe.position.z), center))
            continue;

        const char letter[] = { ProbeContribution::GetCornerLetter(corner), '\0' };
        char weight[16];
        sprintf_s(weight, " %.1f%%", probe.weight * 100.0f);
        const ImVec2 letterExtent = ImGui::GetFont()->CalcTextSizeA(letterSize, FLT_MAX, 0.0f, letter);
        const ImVec2 weightExtent = ImGui::GetFont()->CalcTextSizeA(weightSize, FLT_MAX, 0.0f, weight);
        const ImVec2 origin(center.x + padding.x, center.y - letterExtent.y - padding.y);
        const ImVec2 end(origin.x + letterExtent.x + weightExtent.x + 2.0f * padding.x, origin.y + letterExtent.y + 2.0f * padding.y);

        drawList->AddRectFilled(origin, end, backgroundColor);
        if (labels.hoveredCorner == corner)
            drawList->AddRect(origin, end, hoveredBorderColor, 0.0f, 0, 2.0f * labels.scale);
        const ImVec2 letterPosition(origin.x + padding.x, origin.y + padding.y);
        drawList->AddText(ImGui::GetFont(), letterSize, letterPosition, textColor, letter);
        drawList->AddText(ImGui::GetFont(), weightSize, ImVec2(letterPosition.x + letterExtent.x, letterPosition.y + letterExtent.y - weightExtent.y),
            textColor, weight);
    }

    ImVec2 point;
    if (SceneEditor::ProjectToScene(camera, Vector3(contribution.position.x, contribution.position.y, contribution.position.z), point))
    {
        const float pointRadius = 5.0f;
        drawList->AddCircleFilled(point, pointRadius, IM_COL32(255, 255, 255, 255));
        drawList->AddCircle(point, pointRadius, IM_COL32(0, 0, 0, 255), 0, 2.0f);
    }
    drawList->PopClipRect();
}
