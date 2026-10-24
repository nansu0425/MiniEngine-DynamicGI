#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"
#include "Core/Shaders/Random.hlsli"

// PathTracer.cpp의 Constants와 같은 layout이다.
cbuffer Constants : register(b0)
{
    CameraRays Rays;
    uint Seed;
    // reset 뒤 이번 frame 앞까지 dispatch한 sample 수. 이번 frame의 sample 번호는 여기서부터 센다.
    uint FirstSample;
    // 이번 frame에 pixel마다 더하는 sample 수. 0이면 누적한 평균을 그대로 보인다.
    uint NumSamples;
};

// u register의 순서는 PathTracer.cpp의 UAV와 같다.
// rgb는 지금까지 더한 sample의 평균, a는 더한 sample 수다.
RWTexture2D<float4> Accumulation : register(u0);
RWTexture2D<float3> SceneColor : register(u1);

// shading이 아직 없어 처음 맞은 곳의 diffuse albedo를 radiance로 둔다.
float3 TraceRadiance(RayDesc ray)
{
    RayHit hit;
    if (!TraceClosestHit(ray, kCullSingleSidedBackFaces, hit))
        return 0.0;

    const HitSurface surface = GetHitSurface(hit);
    return surface.baseColor.rgb * (1.0 - surface.metallic);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    // reset 뒤 첫 sample은 앞의 내용을 읽지 않고 덮어쓴다.
    float4 accumulated = FirstSample == 0 ? 0.0 : Accumulation[pixel];
    for (uint i = 0; i < NumSamples; ++i)
    {
        Random random = MakeRandom(Seed, pixel, FirstSample + i);
        // pixel 격자의 [x, x + 1) x [y, y + 1)에서 고르게 고른 위치라 평균이 pixel의 box filter가 된다.
        const float3 radiance = TraceRadiance(GetCameraRay(Rays, float2(pixel) + random.rand2()));
        accumulated.a += 1.0;
        accumulated.rgb += (radiance - accumulated.rgb) / accumulated.a;
    }
    Accumulation[pixel] = accumulated;
    SceneColor[pixel] = accumulated.rgb;
}
