//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:   James Stanard
//

#include "Model/Renderer/Renderer.h"
#include "Model/Model.h"
#include "Core/Graphics/Texture/TextureManager.h"
#include "Model/ConstantBuffers.h"
#include "Model/Renderer/LightManager.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/GraphicsCommon.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Camera/ShadowCamera.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Resource/ColorBuffer.h"

#include "Output/Model/CompiledShaders/DefaultVS.h"
#include "Output/Model/CompiledShaders/DefaultSkinVS.h"
#include "Output/Model/CompiledShaders/TransparentPS.h"
#include "Output/Model/CompiledShaders/DefaultNoUV1VS.h"
#include "Output/Model/CompiledShaders/DefaultNoUV1SkinVS.h"
#include "Output/Model/CompiledShaders/TransparentNoUV1PS.h"
#include "Output/Model/CompiledShaders/DefaultNoTangentVS.h"
#include "Output/Model/CompiledShaders/DefaultNoTangentSkinVS.h"
#include "Output/Model/CompiledShaders/TransparentNoTangentPS.h"
#include "Output/Model/CompiledShaders/DefaultNoTangentNoUV1VS.h"
#include "Output/Model/CompiledShaders/DefaultNoTangentNoUV1SkinVS.h"
#include "Output/Model/CompiledShaders/TransparentNoTangentNoUV1PS.h"
#include "Output/Model/CompiledShaders/DepthOnlyVS.h"
#include "Output/Model/CompiledShaders/DepthOnlySkinVS.h"
#include "Output/Model/CompiledShaders/CutoutDepthVS.h"
#include "Output/Model/CompiledShaders/CutoutDepthSkinVS.h"
#include "Output/Model/CompiledShaders/CutoutDepthPS.h"
#include "Output/Model/CompiledShaders/SkyBackgroundVS.h"
#include "Output/Model/CompiledShaders/SkyBackgroundPS.h"
#include "Output/Model/CompiledShaders/GBufferPS.h"
#include "Output/Model/CompiledShaders/GBufferNoUV1PS.h"
#include "Output/Model/CompiledShaders/GBufferNoTangentPS.h"
#include "Output/Model/CompiledShaders/GBufferNoTangentNoUV1PS.h"
#include "Output/Model/CompiledShaders/OpaqueShadingCS.h"

#pragma warning(disable:4319) // '~': zero extending 'uint32_t' to 'uint64_t' of greater size

using namespace Math;
using namespace Graphics;
using namespace Renderer;

namespace Renderer
{
    bool s_Initialized = false;

    DescriptorHeap s_TextureHeap;
    DescriptorHeap s_SamplerHeap;
    std::vector<GraphicsPSO> sm_PSOs;

    uint32_t g_SSAOFullScreenID;
    uint32_t g_ShadowBufferID;

    RootSignature m_RootSig;
    GraphicsPSO m_DefaultPSO(L"Renderer: Default PSO"); // Not finalized.  Used as a template.
    GraphicsPSO m_SkyBackgroundPSO(L"Renderer: Sky Background PSO");

    DescriptorHandle m_CommonTextures;

    // m_CommonTextures에서 t18의 자리
    const uint32_t kIndirectRadianceSlot = 8;
    // t18이 가리키는 buffer와, view를 복사했을 때 그 buffer의 version ID. nullptr이면 black texture를 가리킨다.
    const ColorBuffer* s_IndirectRadiance = nullptr;
    uint32_t s_IndirectRadianceID = 0;
    const ColorBuffer* s_IndirectDiffuseProbes = nullptr;
    IrradianceProbeGrid s_IndirectDiffuseProbeGrid = {};
}

namespace
{
    // sm_PSOs의 앞쪽에 고정한 shadow PSO의 배치다. GetPSO는 color PSO를 그 뒤에 붙인다.
    enum CullMode { kCullFront, kCullNone, kNumCullModes };
    const uint32_t kNumShadowTemplates = 4;
    const uint32_t kNumShadowPSOs = (uint32_t)kNumCullModes * kNumShadowTemplates;

    uint32_t ShadowPSOIndex(CullMode cull, bool skinned, bool alphaTest)
    {
        uint32_t shadowTemplate = (skinned ? 2 : 0) + (alphaTest ? 1 : 0);
        return (uint32_t)cull * kNumShadowTemplates + shadowTemplate;
    }

    enum OpaqueShadingRootParameter { kOpaqueShadingConstants, kOpaqueShadingInputs, kOpaqueShadingOutput, kNumOpaqueShadingRootParameters };
    RootSignature s_OpaqueShadingRootSig;
    ComputePSO s_OpaqueShadingPSO(L"Renderer: Opaque Shading PSO");

    __declspec(align(16)) struct OpaqueShadingConstants
    {
        Matrix4 ShadowMatrix;
        XMFLOAT3 ViewerPos;
        uint32_t Width;
        XMFLOAT3 LightDirection;
        uint32_t Height;
        XMFLOAT3 LightIlluminance;
        uint32_t UseIrradianceProbes;
        IrradianceProbeGrid Probes;
    };
}

void Renderer::Initialize(void)
{
    if (s_Initialized)
        return;

    SamplerDesc DefaultSamplerDesc;
    DefaultSamplerDesc.MaxAnisotropy = 8;

    SamplerDesc CubeMapSamplerDesc = DefaultSamplerDesc;
    //CubeMapSamplerDesc.MaxLOD = 6.0f;

    m_RootSig.Reset(kNumRootBindings, 3);
    m_RootSig.InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.InitStaticSampler(11, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.InitStaticSampler(12, CubeMapSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[kMeshConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
    m_RootSig[kMaterialConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[kMaterialSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[kMaterialSamplers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[kCommonSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[kCommonCBV].InitAsConstantBuffer(1);
    m_RootSig[kSkinMatrices].InitAsBufferSRV(20, D3D12_SHADER_VISIBILITY_VERTEX);
    m_RootSig.Finalize(L"RootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
    DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();

    D3D12_INPUT_ELEMENT_DESC posOnly[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_ELEMENT_DESC posAndUV[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_ELEMENT_DESC skinPos[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_ELEMENT_DESC skinPosAndUV[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    ASSERT(sm_PSOs.size() == 0);

    // Depth Only PSOs

    GraphicsPSO DepthOnlyPSO(L"Renderer: Depth Only PSO");
    DepthOnlyPSO.SetRootSignature(m_RootSig);
    DepthOnlyPSO.SetBlendState(BlendDisable);
    DepthOnlyPSO.SetDepthStencilState(DepthStateReadWrite);
    DepthOnlyPSO.SetInputLayout(_countof(posOnly), posOnly);
    DepthOnlyPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    DepthOnlyPSO.SetVertexShader(g_pDepthOnlyVS, sizeof(g_pDepthOnlyVS));

    GraphicsPSO CutoutDepthPSO(L"Renderer: Cutout Depth PSO");
    CutoutDepthPSO = DepthOnlyPSO;
    CutoutDepthPSO.SetInputLayout(_countof(posAndUV), posAndUV);
    CutoutDepthPSO.SetVertexShader(g_pCutoutDepthVS, sizeof(g_pCutoutDepthVS));
    CutoutDepthPSO.SetPixelShader(g_pCutoutDepthPS, sizeof(g_pCutoutDepthPS));

    GraphicsPSO SkinDepthOnlyPSO = DepthOnlyPSO;
    SkinDepthOnlyPSO.SetInputLayout(_countof(skinPos), skinPos);
    SkinDepthOnlyPSO.SetVertexShader(g_pDepthOnlySkinVS, sizeof(g_pDepthOnlySkinVS));

    GraphicsPSO SkinCutoutDepthPSO = CutoutDepthPSO;
    SkinCutoutDepthPSO.SetInputLayout(_countof(skinPosAndUV), skinPosAndUV);
    SkinCutoutDepthPSO.SetVertexShader(g_pCutoutDepthSkinVS, sizeof(g_pCutoutDepthSkinVS));

    const GraphicsPSO* ShadowTemplates[2][2] =
    {
        { &DepthOnlyPSO, &CutoutDepthPSO },
        { &SkinDepthOnlyPSO, &SkinCutoutDepthPSO },
    };
    const D3D12_RASTERIZER_DESC* ShadowRasterizers[kNumCullModes] = { &RasterizerShadowCullFront, &RasterizerShadowTwoSided };
    const DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();
    ASSERT(ShadowFormat != DXGI_FORMAT_UNKNOWN);

    sm_PSOs.resize(kNumShadowPSOs);
    for (uint32_t cull = 0; cull < kNumCullModes; ++cull)
    {
        for (bool skinned : { false, true })
        {
            for (bool alphaTest : { false, true })
            {
                GraphicsPSO ShadowPSO = *ShadowTemplates[skinned][alphaTest];
                ShadowPSO.SetRasterizerState(*ShadowRasterizers[cull]);
                ShadowPSO.SetRenderTargetFormats(0, nullptr, ShadowFormat);
                ShadowPSO.Finalize();
                sm_PSOs[ShadowPSOIndex((CullMode)cull, skinned, alphaTest)] = ShadowPSO;
            }
        }
    }

    // Default PSO

    m_DefaultPSO.SetRootSignature(m_RootSig);
    m_DefaultPSO.SetRasterizerState(RasterizerDefault);
    m_DefaultPSO.SetBlendState(BlendDisable);
    m_DefaultPSO.SetDepthStencilState(DepthStateReadWrite);
    m_DefaultPSO.SetInputLayout(0, nullptr);
    m_DefaultPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_DefaultPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
    m_DefaultPSO.SetVertexShader(g_pDefaultVS, sizeof(g_pDefaultVS));
    m_DefaultPSO.SetPixelShader(g_pTransparentPS, sizeof(g_pTransparentPS));

    m_SkyBackgroundPSO = m_DefaultPSO;
    m_SkyBackgroundPSO.SetDepthStencilState(DepthStateReadOnly);
    m_SkyBackgroundPSO.SetVertexShader(g_pSkyBackgroundVS, sizeof(g_pSkyBackgroundVS));
    m_SkyBackgroundPSO.SetPixelShader(g_pSkyBackgroundPS, sizeof(g_pSkyBackgroundPS));
    m_SkyBackgroundPSO.Finalize();

    s_OpaqueShadingRootSig.Reset(kNumOpaqueShadingRootParameters, 2);
    s_OpaqueShadingRootSig[kOpaqueShadingConstants].InitAsConstantBuffer(0);
    s_OpaqueShadingRootSig[kOpaqueShadingInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, kNumGBufferTargets + 3);
    s_OpaqueShadingRootSig[kOpaqueShadingOutput].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_OpaqueShadingRootSig.InitStaticSampler(0, SamplerShadowDesc);
    s_OpaqueShadingRootSig.InitStaticSampler(1, SamplerLinearClampDesc);
    s_OpaqueShadingRootSig.Finalize(L"Renderer: Opaque Shading");

    s_OpaqueShadingPSO.SetRootSignature(s_OpaqueShadingRootSig);
    s_OpaqueShadingPSO.SetComputeShader(g_pOpaqueShadingCS, sizeof(g_pOpaqueShadingCS));
    s_OpaqueShadingPSO.Finalize();

    TextureManager::Initialize(L"");

    s_TextureHeap.Create(L"Scene Texture Descriptors", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096);

    // Maybe only need 2 for wrap vs. clamp?  Currently we allocate 1 for 1 with textures
    s_SamplerHeap.Create(L"Scene Sampler Descriptors", D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048);

    Lighting::InitializeResources();

    // Allocate a descriptor table for the common textures
    m_CommonTextures = s_TextureHeap.Alloc(9);

    uint32_t DestCount = 9;
    uint32_t SourceCounts[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    // t10 ~ t12는 이 renderer의 shader가 읽지 않는다. LEGACY_RENDERER의 Sponza shader가 t12와 그 뒤를 이 번호로 읽으므로 자리를 둔다.
    D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
    {
        GetDefaultTexture(kBlackCubeMap),
        GetDefaultTexture(kBlackCubeMap),
        g_SSAOFullScreen.GetSRV(),
        g_ShadowBuffer.GetSRV(),
        Lighting::m_LightBuffer.GetSRV(),
        Lighting::m_LightShadowArray.GetSRV(),
        Lighting::m_LightGrid.GetSRV(),
        Lighting::m_LightGridBitMask.GetSRV(),
        GetDefaultTexture(kBlackOpaque2D),
    };

    g_Device->CopyDescriptors(1, &m_CommonTextures, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    g_SSAOFullScreenID = g_SSAOFullScreen.GetVersionID();
    g_ShadowBufferID = g_ShadowBuffer.GetVersionID();

    s_Initialized = true;
}

void Renderer::UpdateGlobalDescriptors(void)
{
    if (g_SSAOFullScreenID == g_SSAOFullScreen.GetVersionID() &&
        g_ShadowBufferID == g_ShadowBuffer.GetVersionID())
    {
        return;
    }

    uint32_t DestCount = 2;
    uint32_t SourceCounts[] = { 1, 1 };

    D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
    {
        g_SSAOFullScreen.GetSRV(),
        g_ShadowBuffer.GetSRV(),
    };

    DescriptorHandle dest = m_CommonTextures + 2 * s_TextureHeap.GetDescriptorSize();

    g_Device->CopyDescriptors(1, &dest, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    g_SSAOFullScreenID = g_SSAOFullScreen.GetVersionID();
    g_ShadowBufferID = g_ShadowBuffer.GetVersionID();

}

void Renderer::SetIndirectRadiance(const ColorBuffer* radiance)
{
    const uint32_t versionID = radiance != nullptr ? radiance->GetVersionID() : 0;
    if (radiance == s_IndirectRadiance && versionID == s_IndirectRadianceID)
        return;

    // 앞 frame의 draw가 읽고 있을 수 있는 descriptor를 덮어쓰므로 GPU를 기다린다. 항을 켜고 끄거나 buffer를 다시 만들 때만 온다.
    g_CommandManager.IdleGPU();
    const D3D12_CPU_DESCRIPTOR_HANDLE view = radiance != nullptr ? radiance->GetSRV() : GetDefaultTexture(kBlackOpaque2D);
    const DescriptorHandle dest = m_CommonTextures + kIndirectRadianceSlot * s_TextureHeap.GetDescriptorSize();
    g_Device->CopyDescriptorsSimple(1, dest, view, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s_IndirectRadiance = radiance;
    s_IndirectRadianceID = versionID;
}

void Renderer::SetIndirectDiffuseProbes(const ColorBuffer* irradiance, const IrradianceProbeGrid& grid)
{
    s_IndirectDiffuseProbes = irradiance;
    s_IndirectDiffuseProbeGrid = grid;
}

void Renderer::Shutdown(void)
{
    Lighting::Shutdown();
    TextureManager::Shutdown();
    s_TextureHeap.Destroy();
    s_SamplerHeap.Destroy();
}

uint8_t Renderer::GetPSO(uint16_t psoFlags)
{
    using namespace PSOFlags;

    GraphicsPSO ColorPSO = m_DefaultPSO;

    uint16_t Requirements = kHasPosition | kHasNormal;
    ASSERT((psoFlags & Requirements) == Requirements);

    std::vector<D3D12_INPUT_ELEMENT_DESC> vertexLayout;
    if (psoFlags & kHasPosition)
        vertexLayout.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT});
    if (psoFlags & kHasNormal)
        vertexLayout.push_back({"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT});
    if (psoFlags & kHasTangent)
        vertexLayout.push_back({"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT});
    if (psoFlags & kHasUV0)
        vertexLayout.push_back({"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT});
    else
        vertexLayout.push_back({"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       1, D3D12_APPEND_ALIGNED_ELEMENT});
    if (psoFlags & kHasUV1)
        vertexLayout.push_back({"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT});
    if (psoFlags & kHasSkin)
    {
        vertexLayout.push_back({ "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
        vertexLayout.push_back({ "BLENDWEIGHT", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
    }

    ColorPSO.SetInputLayout((uint32_t)vertexLayout.size(), vertexLayout.data());

    if (psoFlags & kHasSkin)
    {
        if (psoFlags & kHasTangent)
        {
            if (psoFlags & kHasUV1)
            {
                ColorPSO.SetVertexShader(g_pDefaultSkinVS, sizeof(g_pDefaultSkinVS));
                ColorPSO.SetPixelShader(g_pTransparentPS, sizeof(g_pTransparentPS));
            }
            else
            {
                ColorPSO.SetVertexShader(g_pDefaultNoUV1SkinVS, sizeof(g_pDefaultNoUV1SkinVS));
                ColorPSO.SetPixelShader(g_pTransparentNoUV1PS, sizeof(g_pTransparentNoUV1PS));
            }
        }
        else
        {
            if (psoFlags & kHasUV1)
            {
                ColorPSO.SetVertexShader(g_pDefaultNoTangentSkinVS, sizeof(g_pDefaultNoTangentSkinVS));
                ColorPSO.SetPixelShader(g_pTransparentNoTangentPS, sizeof(g_pTransparentNoTangentPS));
            }
            else
            {
                ColorPSO.SetVertexShader(g_pDefaultNoTangentNoUV1SkinVS, sizeof(g_pDefaultNoTangentNoUV1SkinVS));
                ColorPSO.SetPixelShader(g_pTransparentNoTangentNoUV1PS, sizeof(g_pTransparentNoTangentNoUV1PS));
            }
        }
    }
    else
    {
        if (psoFlags & kHasTangent)
        {
            if (psoFlags & kHasUV1)
            {
                ColorPSO.SetVertexShader(g_pDefaultVS, sizeof(g_pDefaultVS));
                ColorPSO.SetPixelShader(g_pTransparentPS, sizeof(g_pTransparentPS));
            }
            else
            {
                ColorPSO.SetVertexShader(g_pDefaultNoUV1VS, sizeof(g_pDefaultNoUV1VS));
                ColorPSO.SetPixelShader(g_pTransparentNoUV1PS, sizeof(g_pTransparentNoUV1PS));
            }
        }
        else
        {
            if (psoFlags & kHasUV1)
            {
                ColorPSO.SetVertexShader(g_pDefaultNoTangentVS, sizeof(g_pDefaultNoTangentVS));
                ColorPSO.SetPixelShader(g_pTransparentNoTangentPS, sizeof(g_pTransparentNoTangentPS));
            }
            else
            {
                ColorPSO.SetVertexShader(g_pDefaultNoTangentNoUV1VS, sizeof(g_pDefaultNoTangentNoUV1VS));
                ColorPSO.SetPixelShader(g_pTransparentNoTangentNoUV1PS, sizeof(g_pTransparentNoTangentNoUV1PS));
            }
        }
    }

    if (psoFlags & kAlphaBlend)
    {
        ColorPSO.SetBlendState(BlendPreMultiplied);
        ColorPSO.SetDepthStencilState(DepthStateReadOnly);
    }
    if (psoFlags & kTwoSided)
    {
        ColorPSO.SetRasterizerState(RasterizerTwoSided);
    }
    ColorPSO.Finalize();

    // Look for an existing PSO
    for (uint32_t i = 0; i < sm_PSOs.size(); ++i)
    {
        if (ColorPSO.GetPipelineStateObject() == sm_PSOs[i].GetPipelineStateObject())
        {
            return (uint8_t)i;
        }
    }

    // If not found, keep the new one, and return its index
    sm_PSOs.push_back(ColorPSO);

    // The returned PSO index has read-write depth.  The index+1 writes the G-buffer.
    GraphicsPSO GBufferPSO = ColorPSO;
    if (psoFlags & kHasTangent)
    {
        if (psoFlags & kHasUV1)
            GBufferPSO.SetPixelShader(g_pGBufferPS, sizeof(g_pGBufferPS));
        else
            GBufferPSO.SetPixelShader(g_pGBufferNoUV1PS, sizeof(g_pGBufferNoUV1PS));
    }
    else
    {
        if (psoFlags & kHasUV1)
            GBufferPSO.SetPixelShader(g_pGBufferNoTangentPS, sizeof(g_pGBufferNoTangentPS));
        else
            GBufferPSO.SetPixelShader(g_pGBufferNoTangentNoUV1PS, sizeof(g_pGBufferNoTangentNoUV1PS));
    }
    DXGI_FORMAT GBufferFormats[kNumGBufferTargets];
    for (uint32_t i = 0; i < kNumGBufferTargets; ++i)
        GBufferFormats[i] = g_GBuffer[i].GetFormat();
    GBufferPSO.SetRenderTargetFormats(kNumGBufferTargets, GBufferFormats, g_SceneDepthBuffer.GetFormat());
    GBufferPSO.SetBlendState(BlendDisable);
    GBufferPSO.SetDepthStencilState(DepthStateReadWrite);
    GBufferPSO.Finalize();
#ifdef DEBUG
    for (uint32_t i = 0; i < sm_PSOs.size(); ++i)
        ASSERT(GBufferPSO.GetPipelineStateObject() != sm_PSOs[i].GetPipelineStateObject());
#endif
    sm_PSOs.push_back(GBufferPSO);

    ASSERT(sm_PSOs.size() <= 256, "Ran out of room for unique PSOs");

    return (uint8_t)sm_PSOs.size() - 2;
}

void Renderer::DrawSkyBackground(GraphicsContext& gfxContext, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const Vector3& skyRadiance)
{
    ScopedTimer _prof(L"Sky Background", gfxContext);

    __declspec(align(16)) struct
    {
        Vector3 SkyRadiance;
    } constants = { skyRadiance };

    gfxContext.SetRootSignature(m_RootSig);
    gfxContext.SetPipelineState(m_SkyBackgroundPSO);
    gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, s_TextureHeap.GetHeapPointer());
    gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, s_SamplerHeap.GetHeapPointer());

    gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
    gfxContext.SetViewportAndScissor(viewport, scissor);

    gfxContext.SetDynamicConstantBufferView(kMaterialConstants, sizeof(constants), &constants);
    gfxContext.Draw(3);
}

void Renderer::ShadeOpaqueSurfaces(ComputeContext& context, const BaseCamera& camera, const GlobalConstants& globals, ColorBuffer& sceneColor)
{
    ScopedTimer _prof(L"Opaque Shading", context);

    OpaqueShadingConstants constants;
    constants.ShadowMatrix = globals.ShadowMatrix;
    XMStoreFloat3(&constants.ViewerPos, camera.GetPosition());
    XMStoreFloat3(&constants.LightDirection, globals.LightDirection);
    XMStoreFloat3(&constants.LightIlluminance, globals.LightIlluminance);
    constants.Width = sceneColor.GetWidth();
    constants.Height = sceneColor.GetHeight();
    constants.UseIrradianceProbes = s_IndirectDiffuseProbes != nullptr ? 1 : 0;
    constants.Probes = s_IndirectDiffuseProbeGrid;

    const D3D12_RESOURCE_STATES shaderResource = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_CPU_DESCRIPTOR_HANDLE inputs[kNumGBufferTargets + 3];
    for (uint32_t i = 0; i < kNumGBufferTargets; ++i)
    {
        ColorBuffer& target = g_GBuffer[i];
        context.TransitionResource(target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inputs[i] = target.GetSRV();
    }
    context.TransitionResource(g_ShadowBuffer, shaderResource);
    inputs[kNumGBufferTargets] = g_ShadowBuffer.GetSRV();
    inputs[kNumGBufferTargets + 1] = s_IndirectRadiance != nullptr ? s_IndirectRadiance->GetSRV() : GetDefaultTexture(kBlackOpaque2D);
    inputs[kNumGBufferTargets + 2] = s_IndirectDiffuseProbes != nullptr ? s_IndirectDiffuseProbes->GetSRV() : GetDefaultTexture(kBlackOpaque2D);
    context.TransitionResource(sceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.SetRootSignature(s_OpaqueShadingRootSig);
    context.SetPipelineState(s_OpaqueShadingPSO);
    context.SetDynamicConstantBufferView(kOpaqueShadingConstants, sizeof(constants), &constants);
    context.SetDynamicDescriptors(kOpaqueShadingInputs, 0, _countof(inputs), inputs);
    context.SetDynamicDescriptor(kOpaqueShadingOutput, 0, sceneColor.GetUAV());
    context.Dispatch2D(constants.Width, constants.Height);
}

void MeshSorter::AddMesh( const Mesh& mesh, float distance,
    D3D12_GPU_VIRTUAL_ADDRESS meshCBV,
    D3D12_GPU_VIRTUAL_ADDRESS materialCBV,
    D3D12_GPU_VIRTUAL_ADDRESS bufferPtr,
    const Joint* skeleton)
{
    SortKey key;
    key.value = m_SortObjects.size();

	bool alphaBlend = (mesh.psoFlags & PSOFlags::kAlphaBlend) == PSOFlags::kAlphaBlend;
    bool alphaTest = (mesh.psoFlags & PSOFlags::kAlphaTest) == PSOFlags::kAlphaTest;
    bool skinned = (mesh.psoFlags & PSOFlags::kHasSkin) == PSOFlags::kHasSkin;
    bool twoSided = (mesh.psoFlags & PSOFlags::kTwoSided) == PSOFlags::kTwoSided;

    union float_or_int { float f; uint32_t u; } dist;
    dist.f = Max(distance, 0.0f);

	if (m_BatchType == kShadows)
	{
		if (alphaBlend)
			return;

		key.passID = kOpaque;
		// single-sided material의 앞면을 cull하는 기준은 docs/policy.md의 "camera 밖에서 본 doubleSided"다.
		key.psoIdx = ShadowPSOIndex(twoSided ? kCullNone : kCullFront, skinned, alphaTest);
        key.key = dist.u;
		m_SortKeys.push_back(key.value);
		m_PassCounts[kOpaque]++;
	}
    else if (alphaBlend)
    {
        key.passID = kTransparent;
        key.psoIdx = mesh.pso;
        key.key = ~dist.u;
        m_SortKeys.push_back(key.value);
        m_PassCounts[kTransparent]++;
    }
    else
    {
        key.passID = kOpaque;
        key.psoIdx = mesh.pso + 1;
        key.key = dist.u;
        m_SortKeys.push_back(key.value);
        m_PassCounts[kOpaque]++;
    }

    SortObject object = { &mesh, skeleton, meshCBV, materialCBV, bufferPtr };
    m_SortObjects.push_back(object);
}

void MeshSorter::Sort()
{
    struct { bool operator()(uint64_t a, uint64_t b) const { return a < b; } } Cmp;
    std::sort(m_SortKeys.begin(), m_SortKeys.end(), Cmp);
}

void MeshSorter::RenderMeshes(
    DrawPass pass,
    GraphicsContext& context,
    GlobalConstants& globals)
{
	ASSERT(m_DSV != nullptr);

    Renderer::UpdateGlobalDescriptors();

    context.SetRootSignature(m_RootSig);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, s_TextureHeap.GetHeapPointer());
    context.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, s_SamplerHeap.GetHeapPointer());

    // Set common textures
    context.SetDescriptorTable(kCommonSRVs, m_CommonTextures);

    // Set common shader constants
	globals.ViewProjMatrix = m_Camera->GetViewProjMatrix();
	globals.CameraPos = m_Camera->GetPosition();
	context.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &globals);

	if (m_BatchType == kShadows)
	{
		context.TransitionResource(*m_DSV, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		context.ClearDepth(*m_DSV);
		context.SetDepthStencilTarget(m_DSV->GetDSV());

		if (m_Viewport.Width == 0)
		{
			m_Viewport.TopLeftX = 0.0f;
			m_Viewport.TopLeftY = 0.0f;
			m_Viewport.Width = (float)m_DSV->GetWidth();
			m_Viewport.Height = (float)m_DSV->GetHeight();
			m_Viewport.MaxDepth = 1.0f;
			m_Viewport.MinDepth = 0.0f;

			m_Scissor.left = 1;
			m_Scissor.right = m_DSV->GetWidth() - 2;
			m_Scissor.top = 1;
			m_Scissor.bottom = m_DSV->GetHeight() - 2;
		}
	}
	else
	{
		for (uint32_t i = 0; i < m_NumRTVs; ++i)
		{
			ASSERT(m_RTV[i] != nullptr);
			ASSERT(m_DSV->GetWidth() == m_RTV[i]->GetWidth());
			ASSERT(m_DSV->GetHeight() == m_RTV[i]->GetHeight());
		}

		if (m_Viewport.Width == 0)
		{
			m_Viewport.TopLeftX = 0.0f;
			m_Viewport.TopLeftY = 0.0f;
			m_Viewport.Width = (float)m_DSV->GetWidth();
			m_Viewport.Height = (float)m_DSV->GetHeight();
			m_Viewport.MaxDepth = 1.0f;
			m_Viewport.MinDepth = 0.0f;

			m_Scissor.left = 0;
			m_Scissor.right = m_DSV->GetWidth();
			m_Scissor.top = 0;
			m_Scissor.bottom = m_DSV->GetWidth();
		}
	}

    for ( ; m_CurrentPass <= pass; m_CurrentPass = (DrawPass)(m_CurrentPass + 1))
    {
        const uint32_t passCount = m_PassCounts[m_CurrentPass];
        if (passCount == 0)
            continue;

		if (m_BatchType == kDefault)
		{
			switch (m_CurrentPass)
			{
			case kOpaque:
			{
				D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8];
				for (uint32_t i = 0; i < m_NumRTVs; ++i)
				{
					context.TransitionResource(*m_RTV[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
					rtvs[i] = m_RTV[i]->GetRTV();
				}
				context.TransitionResource(*m_DSV, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				context.SetRenderTargets(m_NumRTVs, rtvs, m_DSV->GetDSV());
				break;
			}
			case kTransparent:
				context.TransitionResource(*m_DSV, D3D12_RESOURCE_STATE_DEPTH_READ);
				context.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.SetRenderTarget(g_SceneColorBuffer.GetRTV(), m_DSV->GetDSV_DepthReadOnly());
				break;
			}
		}

        context.SetViewportAndScissor(m_Viewport, m_Scissor);
        context.FlushResourceBarriers();

        const uint32_t lastDraw = m_CurrentDraw + passCount;

        while (m_CurrentDraw < lastDraw)
        {
            SortKey key;
            key.value = m_SortKeys[m_CurrentDraw];
            const SortObject& object = m_SortObjects[key.objectIdx];
            const Mesh& mesh = *object.mesh;

            context.SetConstantBuffer(kMeshConstants, object.meshCBV);
            context.SetConstantBuffer(kMaterialConstants, object.materialCBV);
            context.SetDescriptorTable(kMaterialSRVs, s_TextureHeap[mesh.srvTable]);
            context.SetDescriptorTable(kMaterialSamplers, s_SamplerHeap[mesh.samplerTable]);
            if (mesh.numJoints > 0)
            {
                ASSERT(object.skeleton != nullptr, "Unspecified joint matrix array");
                context.SetDynamicSRV(kSkinMatrices, sizeof(Joint) * mesh.numJoints, object.skeleton + mesh.startJoint);
            }
            context.SetPipelineState(sm_PSOs[key.psoIdx]);

            if (m_BatchType == kShadows)
            {
                bool alphaTest = (mesh.psoFlags & PSOFlags::kAlphaTest) == PSOFlags::kAlphaTest;
                uint32_t stride = alphaTest ? 20u : 12u;
                if (mesh.numJoints > 0)
                    stride += 16;
                context.SetVertexBuffer(0, {object.bufferPtr + mesh.vbDepthOffset, mesh.vbDepthSize, stride});
            }
            else
            {
                context.SetVertexBuffer(0, {object.bufferPtr + mesh.vbOffset, mesh.vbSize, mesh.vbStride});
            }

            context.SetIndexBuffer({object.bufferPtr + mesh.ibOffset, mesh.ibSize, (DXGI_FORMAT)mesh.ibFormat});

            for (uint32_t i = 0; i < mesh.numDraws; ++i)
                context.DrawIndexed(mesh.draw[i].primCount, mesh.draw[i].startIndex, mesh.draw[i].baseVertex);

            ++m_CurrentDraw;
        }
    }

	if (m_BatchType == kShadows)
	{
		context.TransitionResource(*m_DSV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}
