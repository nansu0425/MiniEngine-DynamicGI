#include "Model/Renderer/GBufferTextures.hlsli"
#include "Model/DynamicGI/HitLighting.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/Random.hlsli"

cbuffer Constants : register(b0)
{
    GILights Lights;
    float3 ViewerPos;
    uint Width;
    uint Height;
    uint Seed;
    // reset 뒤 이번 frame 앞까지 sample 수
    uint SamplesBeforeThisFrame;
    uint SamplesThisFrame;
    uint MaxHits;
    uint RussianRoulette;
};

// rgb는 지금까지 더한 sample의 평균, a는 더한 sample 수다.
RWTexture2D<float4> Accumulation : register(u0);

static const uint kFirstRussianRouletteHit = 3;

float3 ReflectDirectionalLight(HitSurface surface, GltfBrdfInput brdf)
{
    if (all(Lights.lightIlluminance == 0.0))
        return 0.0;
    RayDesc ray;
    if (!DescribeSurfaceRay(surface.position, surface.geometryNormal, Lights.lightDirection, ray) || IsOccluded(ray, kCullSingleSidedBackFaces))
        return 0.0;
    return EvaluateGltfBrdf(brdf, Lights.lightDirection) * Lights.lightIlluminance * max(0.0, dot(surface.shadingNormal, Lights.lightDirection));
}

float3 TraceIncomingRadiance(RayDesc ray, inout Random random)
{
    float3 radiance = 0.0;
    float3 throughput = 1.0;
    for (uint hits = 1;; ++hits)
    {
        RayHit rayHit;
        if (!TraceClosestHit(ray, kCullSingleSidedBackFaces, rayHit))
            return radiance + throughput * Lights.skyRadiance;

        const HitSurface surface = GetHitSurface(rayHit, kSharpestTextureLod);
        const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, -ray.Direction);
        radiance += throughput * (surface.emissive + ReflectDirectionalLight(surface, brdf));

        if (hits == MaxHits)
            return radiance;

        const GltfBrdfSample reflection = SampleGltfBrdfDirection(brdf, float3(random.rand(), random.rand2()));
        if (!DescribeSurfaceRay(surface.position, surface.geometryNormal, reflection.direction, ray))
            return radiance;
        throughput *= reflection.weight;

        if (RussianRoulette != 0 && hits >= kFirstRussianRouletteHit)
        {
            const float survival = min(1.0, max(throughput.r, max(throughput.g, throughput.b)));
            if (random.rand() >= survival)
                return radiance;
            throughput /= survival;
        }
    }
}

float3 EstimateIndirectRadiance(RasterSurface surface, float3 view, inout Random random)
{
    const GltfBrdfInput brdf = MakeGltfBrdfInput(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.shadingNormal, view);
    const GltfBrdfSample reflection = SampleGltfBrdfDirection(brdf, float3(random.rand(), random.rand2()));
    RayDesc ray;
    if (!DescribeSurfaceRay(surface.position, surface.geometryNormal, reflection.direction, ray))
        return 0.0;
    return reflection.weight * TraceIncomingRadiance(ray, random);
}

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height)
        return;

    RasterSurface surface;
    const bool hasSurface = LoadGBuffer(DTid, surface);
    // reset 뒤 첫 sample은 앞의 내용을 읽지 않고 덮어쓴다.
    float4 accumulated = SamplesBeforeThisFrame == 0 ? 0.0 : Accumulation[DTid];
    if (hasSurface)
    {
        const float3 view = normalize(ViewerPos - surface.position);
        for (uint i = 0; i < SamplesThisFrame; ++i)
        {
            Random random = MakeRandom(Seed, DTid, SamplesBeforeThisFrame + i);
            const float3 estimate = EstimateIndirectRadiance(surface, view, random);

            if (!all(isfinite(estimate)))
                continue;

            accumulated.a += 1.0;
            accumulated.rgb += (estimate - accumulated.rgb) / accumulated.a;
        }
    }
    Accumulation[DTid] = accumulated;
}
