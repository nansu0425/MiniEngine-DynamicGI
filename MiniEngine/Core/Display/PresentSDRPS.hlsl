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

#include "Core/Shaders/ShaderUtility.hlsli"
#include "Core/Display/PresentRS.hlsli"

Texture2D<float3> ColorTex : register(t0);
SamplerState PointSampler : register(s1);

[RootSignature(Present_RootSig)]
float3 main( float4 position : SV_Position, float2 uv : TexCoord0 ) : SV_Target0
{
    // position에는 viewport offset이 들어 있으므로 viewport 기준 uv로 읽는다.
    // texture와 viewport의 크기가 같아서 point sampling은 texel을 그대로 가져온다.
    float3 LinearRGB = ColorTex.SampleLevel(PointSampler, uv, 0);
    return ApplyDisplayProfile(LinearRGB, DISPLAY_PLANE_FORMAT);
}
