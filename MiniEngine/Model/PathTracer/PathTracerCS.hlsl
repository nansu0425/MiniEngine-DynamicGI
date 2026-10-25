#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"
#include "Model/Raytracing/SurfaceRayOrigin.hlsli"
#include "Model/PathTracer/LambertBrdf.hlsli"
#include "Core/Shaders/Random.hlsli"

// PathTracer.cpp의 Constants와 같은 layout이다. 뜻은 PathTracer::Settings의 같은 이름의 값을 따른다.
cbuffer Constants : register(b0)
{
    CameraRays Rays;
    uint Seed;
    // reset 뒤 이번 frame 앞까지 dispatch한 sample 수. 이번 frame의 sample 번호는 여기서부터 센다.
    uint FirstSample;
    // 이번 frame에 pixel마다 더하는 sample 수. 0이면 누적한 평균을 그대로 보인다.
    uint NumSamples;
    uint MaxBounces;
    uint RussianRoulette;
    uint GeometricNormals;
    uint Furnace;
    float SkyRadiance;
    float FurnaceAlbedo;
    float FurnaceEmission;
};

// u register의 순서는 PathTracer.cpp의 UAV와 같다.
// rgb는 지금까지 더한 sample의 평균, a는 더한 sample 수다.
RWTexture2D<float4> Accumulation : register(u0);
RWTexture2D<float3> SceneColor : register(u1);

// 몇 번째 반사부터 Russian roulette로 path를 끊는지. 앞의 반사에서 끊으면 아끼는 trace는 적고 분산만 커진다.
static const uint kFirstRussianRouletteBounce = 3;

// path가 맞은 곳에서 반사와 방출에 쓰는 값. furnace와 Geometric Normals는 이 값만 바꿔서, 확인하는 path loop를 그대로 지나게 한다.
struct PathVertex
{
    float3 position;
    float3 geometryNormal;
    // 반사 방향을 고르는 normal
    float3 normal;
    float3 albedo;
    float3 emission;
};

PathVertex GetPathVertex(RayHit hit)
{
    const HitSurface surface = GetHitSurface(hit);

    PathVertex vertex;
    vertex.position = surface.position;
    vertex.geometryNormal = surface.geometryNormal;
    vertex.normal = GeometricNormals != 0 ? surface.geometryNormal : surface.shadingNormal;
    // glTF spec과 다르다: glTF의 metallic-roughness BRDF 대신 albedo가 baseColor x (1 - metallic)인 Lambert로 반사해서,
    // dielectric의 specular와 metal의 반사가 없다. 답을 식으로 아는 furnace로 path loop를 먼저 확인하려고 BRDF를 가장 단순한 것으로 둔다.
    vertex.albedo = Furnace != 0 ? FurnaceAlbedo : surface.baseColor.rgb * (1.0 - surface.metallic);
    vertex.emission = Furnace != 0 ? FurnaceEmission : surface.emissive;
    return vertex;
}

// ray를 따라 camera 쪽으로 오는 radiance를 path 하나로 추정한다.
float3 TraceRadiance(RayDesc ray, inout Random random)
{
    float3 radiance = 0.0;
    float3 throughput = 1.0;
    // bounces는 path가 지금까지 반사한 횟수다. 반사할 때마다 다음 hit의 emission이나 sky를 하나 더 본다.
    for (uint bounces = 0;; ++bounces)
    {
        RayHit hit;
        if (!TraceClosestHit(ray, kCullSingleSidedBackFaces, hit))
            return radiance + throughput * SkyRadiance;

        const PathVertex vertex = GetPathVertex(hit);
        radiance += throughput * vertex.emission;
        if (bounces == MaxBounces)
            return radiance;

        ray.Direction = SampleLambertDirection(vertex.normal, random.rand2());
        // README.md의 "camera 밖에서 본 doubleSided": 면이 geometric normal로 그 방향을 등지면 가려진 것으로 친다.
        if (dot(vertex.geometryNormal, ray.Direction) <= 0.0)
            return radiance;
        // Lambert의 f cos / pdf는 albedo다 (SampleLambertDirection).
        throughput *= vertex.albedo;

        if (RussianRoulette != 0 && bounces + 1 >= kFirstRussianRouletteBounce)
        {
            // 살릴 확률은 1을 넘을 수 없다. 넘는 값으로 나누면 살아남은 path를 덜 키워 bias가 생긴다.
            const float survival = min(1.0, max(throughput.r, max(throughput.g, throughput.b)));
            if (random.rand() >= survival)
                return radiance;
            throughput /= survival;
        }

        ray.Origin = SurfaceRayOrigin(vertex.position, vertex.geometryNormal);
        ray.TMin = 0.0;
        ray.TMax = 1e30;
    }
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
        const RayDesc cameraRay = GetCameraRay(Rays, float2(pixel) + random.rand2());
        const float3 radiance = TraceRadiance(cameraRay, random);
        accumulated.a += 1.0;
        accumulated.rgb += (radiance - accumulated.rgb) / accumulated.a;
    }
    Accumulation[pixel] = accumulated;
    SceneColor[pixel] = accumulated.rgb;
}
