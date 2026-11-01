#ifndef __SURFACE_RAY_ORIGIN_HLSLI__
#define __SURFACE_RAY_ORIGIN_HLSLI__

// surface에서 출발하는 ray가 그 surface에 다시 맞지 않을 origin. Ray Tracing Gems 6장(Wächter, Binder)의 방법이다.
// Capsaicin의 src/core/src/geometry/geometry.hlsl에서 offsetPosition을 가져왔다. license는 THIRD_PARTY_NOTICES.md에 있다.
// 고친 점: 성분마다 옮긴 origin을, 위치의 가장 큰 성분의 크기에 비례하는 거리만큼 normal 쪽으로 옮긴 origin과 비교해 성분마다 더
// 멀리 옮긴 쪽을 쓴다.
// position의 float 정밀도만큼 geometryNormal 쪽으로 옮기므로 scene의 크기와 상관없이 TMin을 0으로 둘 수 있다.
// position은 object space에서 보간해 world로 옮긴 값이어야 오차가 이 거리 안에 든다.
float3 SurfaceRayOrigin(float3 position, float3 geometryNormal)
{
    static const float origin = 1.0f / 32.0f;
    static const float floatScale = 1.0f / 65536.0f;
    static const float intScale = 256.0f;
    // 2^-15 = intScale x 2^-23이라, 가장 큰 성분에 곱하면 그 성분의 intScale ULP와 같은 자릿수의 거리가 된다.
    static const float largestComponentScale = 1.0f / 32768.0f;

    int3 intN = int3(geometryNormal * intScale.xxx);

    float3 positionI = asfloat(asint(position) + select(position < 0, -intN, intN));

    float3 posOffset = position + (geometryNormal * floatScale.xxx);
    const float3 componentOffset = select(abs(position) < origin.xxx, posOffset, positionI);

    // 원래 방법은 성분마다 그 성분의 크기에 맞춰 옮기므로, 0에 가까운 성분은 floatScale만 옮긴다. 그런데 다음 ray가 이 면과 다시
    // 교차하는지 계산하는 오차는 그 성분이 아니라 ray와 삼각형의 모든 좌표의 크기를 따른다 (추측: DXR의 교차 계산이 float32로 ray
    // origin 기준의 좌표를 쓴다고 보고, 그 오차가 가장 큰 좌표의 ULP에 비례한다고 본 것이다). Cornell box에서 y = 0이고 x, z가 512까지 가는
    // 바닥은 floatScale (1.5e-5) 만큼만 옮겨, 바닥이 받는 두 번째 반사가 Mitsuba보다 4.5% 적었다 (scripts/check-external-renderer.py).
    // scene을 옮겨 바닥을 y = 0에서 떼면 차가 없어졌으므로, 바닥에서 쏜 ray가 바닥에 다시 맞았다고 본다 (추측). 그래서 가장 큰 성분의
    // 크기로 정한 거리만큼도 옮긴다.
    const float largest = max(abs(position.x), max(abs(position.y), abs(position.z)));
    const float3 largestOffset = position + geometryNormal * (largest * largestComponentScale);
    return select(geometryNormal * (largestOffset - componentOffset) > 0.0f, largestOffset, componentOffset);
}

#endif
