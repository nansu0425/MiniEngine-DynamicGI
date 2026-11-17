#include "Model/DynamicGI/ProbeReadback.h"
#include "Model/DynamicGI/ProbeLighting.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/GpuBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"

#include "Output/Model/CompiledShaders/ProbeCensusCS.h"

#include <vector>

using namespace Graphics;

namespace
{
    enum CensusRootParameter { kCensusConstants, kCensusInputs, kCensusOutputs, kNumCensusRootParameters };

    struct PendingOffsets
    {
        uint64_t fence;
        uint32_t probeCount;
    };

    struct CensusRequest
    {
        uint32_t id;
        float maxBackFaceRatio;
    };

    struct PendingCensus
    {
        uint64_t fence;
        ProbeReadback::CensusReadback readback;
    };

    __declspec(align(16)) struct CensusConstants
    {
        float maxBackFaceRatio;
        IrradianceProbeGrid grid;
    };

    ReadbackBuffer s_OffsetReadback;
    std::optional<PendingOffsets> s_PendingOffsets;
    uint32_t s_ResetsOfReadOffsets = 0;
    std::vector<DirectX::XMFLOAT4> s_Offsets;

    RootSignature s_CensusRootSignature;
    ComputePSO s_CensusPSO(L"Probe Census");
    ByteAddressBuffer s_Census;
    ReadbackBuffer s_CensusReadback;
    uint32_t s_LastCensusId = 0;
    std::optional<CensusRequest> s_CensusRequest;
    std::optional<PendingCensus> s_PendingCensus;
    std::optional<ProbeReadback::CensusReadback> s_CensusResult;

    void CollectFinishedOffsets(void)
    {
        if (!s_PendingOffsets || !g_CommandManager.IsFenceComplete(s_PendingOffsets->fence))
            return;

        const DirectX::XMFLOAT4* offsets = static_cast<const DirectX::XMFLOAT4*>(s_OffsetReadback.Map());
        s_Offsets.assign(offsets, offsets + s_PendingOffsets->probeCount);
        s_OffsetReadback.Unmap();
        s_PendingOffsets.reset();
    }

    void ReadBackOffsetsOfLastReset(void)
    {
        const uint32_t resets = ProbeLighting::GetProgress().resets;
        if (s_PendingOffsets || resets == s_ResetsOfReadOffsets)
            return;

        const ProbeLighting::Probes probes = ProbeLighting::GetProbes();
        ComputeContext& context = ComputeContext::Begin(L"Probe Offset Readback");
        context.ReadbackTexture(s_OffsetReadback, probes.offset);
        s_PendingOffsets = PendingOffsets{ context.Finish(), GetProbeCount(probes.grid) };
        s_ResetsOfReadOffsets = resets;
    }

    void CollectFinishedCensus(void)
    {
        if (!s_PendingCensus || !g_CommandManager.IsFenceComplete(s_PendingCensus->fence))
            return;

        ProbeReadback::CensusReadback readback = s_PendingCensus->readback;
        std::memcpy(&readback.census, s_CensusReadback.Map(), sizeof(readback.census));
        s_CensusReadback.Unmap();
        s_CensusResult = readback;
        s_PendingCensus.reset();
    }

    void CountRequestedCensus(void)
    {
        const ProbeLighting::Progress progress = ProbeLighting::GetProgress();
        if (!s_CensusRequest || s_PendingCensus || progress.resets == 0)
            return;

        const ProbeLighting::Probes probes = ProbeLighting::GetProbes();
        const CensusConstants constants = { .maxBackFaceRatio = s_CensusRequest->maxBackFaceRatio, .grid = probes.grid };
        ComputeContext& context = ComputeContext::Begin(L"Probe Census");
        {
            ScopedTimer _prof(L"Probe Census", context);

            context.SetRootSignature(s_CensusRootSignature);
            context.SetPipelineState(s_CensusPSO);
            context.TransitionResource(s_Census, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
            context.ClearUAV(s_Census);
            context.TransitionResource(probes.backFaceRatio, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.TransitionResource(probes.offset, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.SetDynamicConstantBufferView(kCensusConstants, sizeof(constants), &constants);
            context.SetDynamicDescriptor(kCensusInputs, 0, probes.backFaceRatio.GetSRV());
            context.SetDynamicDescriptor(kCensusInputs, 1, probes.offset.GetSRV());
            context.SetDynamicDescriptor(kCensusOutputs, 0, s_Census.GetUAV());
            context.Dispatch1D(GetProbeCount(probes.grid), 64);
            context.CopyBuffer(s_CensusReadback, s_Census);
        }
        s_PendingCensus = PendingCensus{ context.Finish(), {
            .id = s_CensusRequest->id,
            .updates = progress.updates,
            .resets = progress.resets,
            .probes = GetProbeCount(probes.grid),
            .maxBackFaceRatio = s_CensusRequest->maxBackFaceRatio,
        } };
        s_CensusRequest.reset();
    }
}

void ProbeReadback::Initialize(void)
{
    s_CensusRootSignature.Reset(kNumCensusRootParameters, 0);
    s_CensusRootSignature[kCensusConstants].InitAsConstantBuffer(0);
    s_CensusRootSignature[kCensusInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
    s_CensusRootSignature[kCensusOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_CensusRootSignature.Finalize(L"Probe Census");

    s_CensusPSO.SetRootSignature(s_CensusRootSignature);
    s_CensusPSO.SetComputeShader(g_pProbeCensusCS, sizeof(g_pProbeCensusCS));
    s_CensusPSO.Finalize();

    const uint32_t censusWords = sizeof(Census) / sizeof(uint32_t);
    s_Census.Create(L"Probe Census", censusWords, sizeof(uint32_t));
    s_CensusReadback.Create(L"Probe Census Readback", censusWords, sizeof(uint32_t));
}

void ProbeReadback::Shutdown(void)
{
    s_OffsetReadback.Destroy();
    s_Census.Destroy();
    s_CensusReadback.Destroy();
}

void ProbeReadback::Update(void)
{
    CollectFinishedOffsets();
    ReadBackOffsetsOfLastReset();
    CollectFinishedCensus();
    CountRequestedCensus();
}

Math::Vector3 ProbeReadback::GetProbePosition(const IrradianceProbeGrid& grid, uint32_t probe)
{
    const Math::Vector3 position = GetProbeGridPosition(grid, probe);
    const std::optional<ProbeRelocationReadback> relocation = FindProbeRelocation(grid, probe);
    if (!relocation)
        return position;
    return position + Math::Vector3(relocation->offset.x, relocation->offset.y, relocation->offset.z);
}

std::optional<ProbeReadback::ProbeRelocationReadback> ProbeReadback::FindProbeRelocation(const IrradianceProbeGrid& grid, uint32_t probe)
{
    if (s_Offsets.size() != GetProbeCount(grid) || probe >= s_Offsets.size())
        return std::nullopt;
    const DirectX::XMFLOAT4& offset = s_Offsets[probe];
    return ProbeRelocationReadback{ DirectX::XMFLOAT3(offset.x, offset.y, offset.z), (ProbeRelocation::Result)(uint32_t)offset.w };
}

uint32_t ProbeReadback::RequestCensus(float maxBackFaceRatio)
{
    s_CensusRequest = CensusRequest{ ++s_LastCensusId, maxBackFaceRatio };
    return s_LastCensusId;
}

const std::optional<ProbeReadback::CensusReadback>& ProbeReadback::GetCensus(void)
{
    CollectFinishedCensus();
    return s_CensusResult;
}
