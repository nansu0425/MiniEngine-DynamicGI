#include "Model/Renderer/Common.hlsli"

cbuffer PSConstants : register(b0)
{
    // sky light의 radiance (cd/m²). 모든 방향에서 같다.
    float3 SkyRadiance;
};

[RootSignature(Renderer_RootSig)]
float4 main() : SV_Target0
{
    return float4(SkyRadiance, 1);
}
