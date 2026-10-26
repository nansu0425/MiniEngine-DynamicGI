#include "Core/pch.h"
#include "Core/Effects/LinearDepth/LinearDepth.h"
#include "Core/Effects/TemporalEffects/TemporalEffects.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Resource/BufferManager.h"

#include "Output/Core/CompiledShaders/LinearizeDepthCS.h"

using namespace Graphics;

namespace
{
    enum RootBindings { kZMagic, kLinearDepthUAV, kDepthSRV, kNumRootBindings };

    RootSignature s_RootSignature;
    ComputePSO s_LinearizeDepthCS(L"Linear Depth: Linearize Depth CS");
}

void LinearDepth::Initialize(void)
{
    s_RootSignature.Reset(kNumRootBindings, 0);
    s_RootSignature[kZMagic].InitAsConstants(0, 1);
    s_RootSignature[kLinearDepthUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSignature[kDepthSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    s_RootSignature.Finalize(L"Linear Depth");

    s_LinearizeDepthCS.SetRootSignature(s_RootSignature);
    s_LinearizeDepthCS.SetComputeShader(g_pLinearizeDepthCS, sizeof(g_pLinearizeDepthCS));
    s_LinearizeDepthCS.Finalize();
}

void LinearDepth::Update(ComputeContext& context, float nearClip, float farClip)
{
    ScopedTimer _prof(L"Linearize Depth", context);

    DepthBuffer& depth = g_SceneDepthBuffer;
    ColorBuffer& linearDepth = g_LinearDepth[TemporalEffects::GetFrameIndexMod2()];

    context.SetRootSignature(s_RootSignature);
    context.TransitionResource(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.SetConstants(kZMagic, (farClip - nearClip) / nearClip);
    context.SetDynamicDescriptor(kDepthSRV, 0, depth.GetDepthSRV());
    context.TransitionResource(linearDepth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.SetDynamicDescriptor(kLinearDepthUAV, 0, linearDepth.GetUAV());
    context.SetPipelineState(s_LinearizeDepthCS);
    context.Dispatch2D(linearDepth.GetWidth(), linearDepth.GetHeight(), 16, 16);
}
