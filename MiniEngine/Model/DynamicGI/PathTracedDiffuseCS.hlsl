#include "Model/DynamicGI/GISurface.hlsli"
#include "Model/DynamicGI/PathTracing.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"

cbuffer Constants : register(b0)
{
    float4x4 ShadowMatrix;
    float3 LightDirection;
    uint Width;
    float3 LightIlluminance;
    uint Height;
    float3 SkyRadiance;
    uint Seed;
    // reset 뒤 이번 frame 앞까지 sample 수
    uint SamplesBeforeThisFrame;
    uint SamplesThisFrame;
    uint MaxHits;
    uint RussianRoulette;
};

StructuredBuffer<GISurface> Surfaces : register(t0);
Texture2D<float> ShadowMap : register(t1);
SamplerComparisonState ShadowMapSampler : register(s0);
// rgb는 지금까지 더한 sample의 평균, a는 더한 sample 수다. 
RWTexture2D<float4> Accumulation : register(u0);

// GI surface가 camera 쪽으로 반사하는 diffuse 간접광의 radiance를 path 하나로 추정한다.
float3 EstimateDiffuseRadiance(GISurface surface, inout Random random)
{
    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor, surface.metallic, surface.roughness, surface.shadingNormal, surface.view);
    const float3 direction = SampleLambertDirection(surface.shadingNormal, random.rand2());
    RayDesc ray;
    if (!DescribeSurfaceRay(surface.position, surface.geometryNormal, direction, ray))
        return 0.0;

    PathTracingLights lights;
    lights.lightDirection = LightDirection;
    lights.lightIlluminance = LightIlluminance;
    lights.skyRadiance = SkyRadiance;
    lights.shadowMatrix = ShadowMatrix;
    
    // pdf = cos / pi -----> BRDF * Li * cos / (cos / pi) = pi * BRDF * Li
    return kPi * EvaluateGltfDiffuseLobe(brdf, direction) * TraceIncomingRadiance(ray, lights, ShadowMap, ShadowMapSampler, MaxHits, RussianRoulette != 0, random);
}

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height)
        return;

    const GISurface surface = Surfaces[DTid.y * Width + DTid.x];
    // reset 뒤 첫 sample은 앞의 내용을 읽지 않고 덮어쓴다.
    float4 accumulated = SamplesBeforeThisFrame == 0 ? 0.0 : Accumulation[DTid];
    if (surface.hit != 0)
    {
        for (uint i = 0; i < SamplesThisFrame; ++i)
        {
            Random random = MakeRandom(Seed, DTid, SamplesBeforeThisFrame + i);
            const float3 estimate = EstimateDiffuseRadiance(surface, random);
            
            if (!all(isfinite(estimate)))
                continue;
            
            accumulated.a += 1.0;
            accumulated.rgb += (estimate - accumulated.rgb) / accumulated.a;
        }
    }
    Accumulation[DTid] = accumulated;
}
