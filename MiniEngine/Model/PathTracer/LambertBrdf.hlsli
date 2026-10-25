/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

// Capsaicin (references/manifest.json의 commit)의 src/core/src/math/sampling.hlsl에서 GetOrthoVectors와
// mapToCosineHemisphere(samples, normal)을 가져왔다. 고친 점: mapToCosineHemisphere를 SampleLambertDirection으로 이름을 바꾸고
// 인자 순서를 바꿨다. squared()와 TWO_PI는 풀어 썼다.

#ifndef __LAMBERT_BRDF_HLSLI__
#define __LAMBERT_BRDF_HLSLI__

void GetOrthoVectors(in float3 n, out float3 b1, out float3 b2)
{
    bool sel = abs(n.z) > 0;
    float3 p2 = sel ? n : n.zyx;
    float k = 1.0f / sqrt(p2.z * p2.z + n.y * n.y);
    b1 = float3(0.0f, -p2.z * k, n.y * k);
    b1 = sel ? b1 : b1.zyx;
    b2 = cross(n, b1);
}

// normal 둘레의 cosine-weighted hemisphere에서 고른 방향. pdf가 cos / π라서 Lambert의 f cos / pdf는 albedo가 된다.
// samples는 [0, 1)의 두 수다.
float3 SampleLambertDirection(float3 normal, float2 samples)
{
    float3 u, v;
    GetOrthoVectors(normal, u, v);

    float phi = 6.28318530717958647692f * samples.y;
    float sinPhi, cosPhi;
    sincos(phi, sinPhi, cosPhi);
    float sinTheta = sqrt(samples.x);
    float cosTheta = sqrt(1.0f - samples.x);

    return normalize(u * sinTheta * cosPhi + v * sinTheta * sinPhi + normal * cosTheta);
}

#endif
