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

#ifndef __SURFACE_RAY_ORIGIN_HLSLI__
#define __SURFACE_RAY_ORIGIN_HLSLI__

// surface에서 출발하는 ray가 그 surface에 다시 맞지 않을 origin.
// Capsaicin의 src/core/src/geometry/geometry.hlsl에 있는 offsetPosition이고, Ray Tracing Gems 6장(Wächter, Binder)의 방법이다.
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
