#include "Model/Renderer/Common.hlsli"

// 화면을 덮는 triangle 하나를 far plane (reversed-Z의 depth 0)에 둔다. depth를 읽기만 하고 GREATER_EQUAL로 비교하므로, 아무 면도 그리지
// 않아 clear한 depth 0이 남은 pixel만 통과한다.
[RootSignature(Renderer_RootSig)]
float4 main(uint vertexID : SV_VertexID) : SV_POSITION
{
    const float2 screenUV = float2(uint2(vertexID, vertexID << 1) & 2);
    return float4(lerp(float2(-1, 1), float2(1, -1), screenUV), 0, 1);
}
