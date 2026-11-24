#include "Model/Renderer/Common.hlsli"
#include "Core/Shaders/SceneLights.hlsli"

cbuffer PSConstants : register(b0)
{
    SceneLights Lights;
};

[RootSignature(Renderer_RootSig)]
float4 main() : SV_Target0
{
    return float4(Lights.skyRadiance, 1);
}
