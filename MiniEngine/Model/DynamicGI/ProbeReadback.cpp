#include "Model/DynamicGI/ProbeReadback.h"
#include "Model/DynamicGI/ProbeLighting.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"

#include <optional>
#include <vector>

using namespace Graphics;

namespace
{
    struct PendingOffsets
    {
        uint64_t fence;
        uint32_t probeCount;
    };

    ReadbackBuffer s_OffsetReadback;
    std::optional<PendingOffsets> s_PendingOffsets;
    uint32_t s_ResetsOfReadOffsets = 0;
    std::vector<DirectX::XMFLOAT4> s_Offsets;

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
}

void ProbeReadback::Shutdown(void)
{
    s_OffsetReadback.Destroy();
}

void ProbeReadback::Update(void)
{
    CollectFinishedOffsets();
    ReadBackOffsetsOfLastReset();
}

Math::Vector3 ProbeReadback::GetProbePosition(const IrradianceProbeGrid& grid, uint32_t probe)
{
    const Math::Vector3 position = GetProbeGridPosition(grid, probe);
    if (s_Offsets.size() != GetProbeCount(grid))
        return position;
    const DirectX::XMFLOAT4& offset = s_Offsets[probe];
    return position + Math::Vector3(offset.x, offset.y, offset.z);
}
