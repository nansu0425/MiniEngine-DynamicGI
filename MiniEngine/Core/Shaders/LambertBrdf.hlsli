#ifndef __LAMBERT_BRDF_HLSLI__
#define __LAMBERT_BRDF_HLSLI__

// Capsaicin의 src/core/src/math/sampling.hlsl에서 GetOrthoVectors와 mapToCosineHemisphere(samples, normal)을 가져왔다.
// license는 THIRD_PARTY_NOTICES.md에 있다. 고친 점: mapToCosineHemisphere를 SampleLambertDirection으로 이름을 바꾸고
// 인자 순서를 바꿨다. squared()와 TWO_PI는 풀어 썼다.
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
