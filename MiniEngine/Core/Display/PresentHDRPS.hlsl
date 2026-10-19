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
// Author:  James Stanard 
//

#include "Core/Display/PresentRS.hlsli"
#include "Core/Shaders/ColorSpaceUtility.hlsli"

Texture2D<float3> MainBuffer : register(t0);
SamplerState PointSampler : register(s1);

[RootSignature(Present_RootSig)]
float3 main( float4 position : SV_Position, float2 uv : TexCoord0 ) : SV_Target0
{
    // PresentSDRPS와 같은 이유로 viewport 기준 uv로 읽는다.
    return ApplyREC2084Curve(MainBuffer.SampleLevel(PointSampler, uv, 0) / 10000.0);
}
