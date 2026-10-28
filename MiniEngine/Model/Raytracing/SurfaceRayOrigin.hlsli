#ifndef __SURFACE_RAY_ORIGIN_HLSLI__
#define __SURFACE_RAY_ORIGIN_HLSLI__

// surface에서 출발하는 ray가 그 surface에 다시 맞지 않을 origin. Ray Tracing Gems 6장(Wächter, Binder)의 방법이다.
// Capsaicin의 src/core/src/geometry/geometry.hlsl에서 offsetPosition을 가져왔다. license는 THIRD_PARTY_NOTICES.md에 있다.
// position의 float 정밀도만큼 geometryNormal 쪽으로 옮기므로 scene의 크기와 상관없이 TMin을 0으로 둘 수 있다.
// position은 object space에서 보간해 world로 옮긴 값이어야 오차가 이 거리 안에 든다.
float3 SurfaceRayOrigin(float3 position, float3 geometryNormal)
{
    static const float origin = 1.0f / 32.0f;
    static const float floatScale = 1.0f / 65536.0f;
    static const float intScale = 256.0f;

    int3 intN = int3(geometryNormal * intScale.xxx);

    float3 positionI = asfloat(asint(position) + select(position < 0, -intN, intN));

    float3 posOffset = position + (geometryNormal * floatScale.xxx);
    return select(abs(position) < origin.xxx, posOffset, positionI);
}

#endif
