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
#include "Core/Math/Common.h"

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

    struct PendingRays
    {
        uint64_t fence;
        size_t rowSize;
        uint32_t raysPerProbe;
        ProbeReadback::ProbeRaysReadback readback;
    };

    constexpr float kProbeRayMissDistance = 1e30f;

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

    ReadbackBuffer s_RayReadback;
    std::optional<PendingRays> s_PendingRays;
    std::optional<ProbeReadback::ProbeRaysReadback> s_Rays;

    void CollectFinishedRays(void)
    {
        if (!s_PendingRays || !g_CommandManager.IsFenceComplete(s_PendingRays->fence))
            return;

        const uint8_t* rows = static_cast<const uint8_t*>(s_RayReadback.Map());
        const DirectX::XMFLOAT4* weightedDirections = reinterpret_cast<const DirectX::XMFLOAT4*>(rows);
        const DirectX::XMFLOAT4* samples = reinterpret_cast<const DirectX::XMFLOAT4*>(rows + s_PendingRays->rowSize);
        ProbeReadback::ProbeRaysReadback readback = std::move(s_PendingRays->readback);
        readback.rays.reserve(s_PendingRays->raysPerProbe);
        for (uint32_t ray = 0; ray < s_PendingRays->raysPerProbe; ++ray)
        {
            const DirectX::XMFLOAT4& weightedDirection = weightedDirections[ray];
            const DirectX::XMFLOAT4& sample = samples[ray];
            readback.rays.push_back({
                .direction = DirectX::XMFLOAT3(weightedDirection.x, weightedDirection.y, weightedDirection.z),
                .weight = weightedDirection.w,
                .radiance = DirectX::XMFLOAT3(sample.x, sample.y, sample.z),
                .hitDistance = sample.w < kProbeRayMissDistance ? std::optional<float>(sample.w) : std::nullopt,
            });
        }
        s_RayReadback.Unmap();
        s_Rays = std::move(readback);
        s_PendingRays.reset();
    }

    void CopyRowOfProbe(ComputeContext& context, ColorBuffer& rows, uint32_t probe, size_t offset)
    {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        footprint.Offset = offset;
        footprint.Footprint.Format = rows.GetFormat();
        footprint.Footprint.Width = rows.GetWidth();
        footprint.Footprint.Height = 1;
        footprint.Footprint.Depth = 1;
        footprint.Footprint.RowPitch = (UINT)Math::AlignUp(rows.GetWidth() * sizeof(DirectX::XMFLOAT4), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const CD3DX12_TEXTURE_COPY_LOCATION destination(s_RayReadback.GetResource(), footprint);
        const CD3DX12_TEXTURE_COPY_LOCATION source(rows.GetResource(), 0);
        const D3D12_BOX rowOfProbe = { 0, probe, 0, rows.GetWidth(), probe + 1, 1 };
        context.TransitionResource(rows, D3D12_RESOURCE_STATE_COPY_SOURCE, true);
        context.GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, &rowOfProbe);
    }

    bool AreRaysOfLastUpdate(const ProbeReadback::ProbeRaysReadback& rays, uint32_t probe)
    {
        const ProbeLighting::Progress progress = ProbeLighting::GetProgress();
        return rays.probe == probe && rays.updates == progress.updates && rays.resets == progress.resets;
    }

    void ReadBackRaysOfLastUpdate(std::optional<uint32_t> probe)
    {
        const ProbeLighting::Probes probes = ProbeLighting::GetProbes();
        if (!probe || s_PendingRays || ProbeLighting::GetProgress().updates == 0 || *probe >= GetProbeCount(probes.grid))
            return;
        if (s_Rays && AreRaysOfLastUpdate(*s_Rays, *probe))
            return;

        const size_t rowSize = Math::AlignUp(probes.raysPerProbe * sizeof(DirectX::XMFLOAT4), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (s_RayReadback.GetBufferSize() != 2 * rowSize)
            s_RayReadback.Create(L"Probe Ray Readback", (uint32_t)(2 * rowSize), 1);

        const ProbeLighting::Progress progress = ProbeLighting::GetProgress();
        ComputeContext& context = ComputeContext::Begin(L"Probe Ray Readback");
        CopyRowOfProbe(context, probes.weightedRayDirections, *probe, 0);
        CopyRowOfProbe(context, probes.raySamples, *probe, rowSize);
        s_PendingRays = PendingRays{ context.Finish(), rowSize, probes.raysPerProbe, {
            .probe = *probe,
            .updates = progress.updates,
            .resets = progress.resets,
            .uniformRaysPerProbe = probes.uniformRaysPerProbe,
        } };
    }

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
    s_RayReadback.Destroy();
}

void ProbeReadback::Update(std::optional<uint32_t> probeOfWatchedRays)
{
    CollectFinishedRays();
    ReadBackRaysOfLastUpdate(probeOfWatchedRays);
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

const ProbeReadback::ProbeRaysReadback* ProbeReadback::FindRaysOfLastUpdate(uint32_t probe)
{
    return s_Rays && AreRaysOfLastUpdate(*s_Rays, probe) ? &*s_Rays : nullptr;
}

const ProbeReadback::ProbeRaysReadback* ProbeReadback::FindLastReadRays(uint32_t probe)
{
    return s_Rays && s_Rays->probe == probe && s_Rays->resets == ProbeLighting::GetProgress().resets ? &*s_Rays : nullptr;
}

void ProbeReadback::ListResources(std::vector<GIProfiling::Resource>& resources)
{
    resources.push_back({ &s_OffsetReadback, GIProfiling::ResourceRole::kDebug });
    resources.push_back({ &s_RayReadback, GIProfiling::ResourceRole::kDebug });
    resources.push_back({ &s_Census, GIProfiling::ResourceRole::kDebug });
    resources.push_back({ &s_CensusReadback, GIProfiling::ResourceRole::kDebug });
}
