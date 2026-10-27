#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/HitSurface.hlsli"
#include "Model/Raytracing/SurfaceRay.hlsli"
#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/LambertBrdf.hlsli"
#include "Core/Shaders/Random.hlsli"

// 1이면 Material Lambert, 0이면 glTF로 반사한다. PathTracerLambertCS.hlsl이 1로 정의한다. Material을 constant로 고르는 shader 하나로
// 두었을 때는 Lambert의 결과가 작업 8 전과 반올림만큼 달라 bit까지 같지 않았다 (scripts/check-gltf-brdf.py의 [S5]). compiler가 두 BRDF의
// code와 함께 Lambert의 연산을 다르게 묶은 것으로 본다 (추측).
#ifndef LAMBERT_MATERIAL
#define LAMBERT_MATERIAL 0
#endif

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
    float FurnaceAlbedo;
    float FurnaceEmission;
    float Padding;
    // surface에서 directional light를 향하는 단위 vector와, light에 수직인 면의 illuminance (lux)
    float3 LightDirection;
    float3 LightIlluminance;
    // ray가 아무 면도 만나지 않을 때 받는 sky light의 radiance (cd/m²). 모든 방향에서 같다.
    float3 SkyRadiance;
};

// u register의 순서는 PathTracer.cpp의 UAV와 같다.
// rgb는 지금까지 더한 sample의 평균, a는 더한 sample 수다. NaN이나 Inf인 sample은 더하지 않으므로, dispatch한 sample 수와 a의 차가 그 수다.
RWTexture2D<float4> Accumulation : register(u0);
RWTexture2D<float3> SceneColor : register(u1);

static const float kPi = 3.14159265358979323846;

// 몇 번째 반사부터 Russian roulette로 path를 끊는지. 앞의 반사에서 끊으면 아끼는 trace는 적고 분산만 커진다.
static const uint kFirstRussianRouletteBounce = 3;

// path가 맞은 곳에서 반사와 방출에 쓰는 값. furnace와 Geometric Normals는 이 값만 바꿔서, 확인하는 path loop를 그대로 지나게 한다.
struct PathVertex
{
    float3 position;
    float3 geometryNormal;
    // 반사 방향을 고르는 normal
    float3 normal;
#if LAMBERT_MATERIAL
    float3 albedo;
#else
    // 이 vertex의 directional light와 다음 방향 고르기가 같은 값을 쓴다.
    GltfBrdfInput brdf;
    // BRDF에 곱하는 값. furnace에서만 Furnace Albedo이고 그 밖에서는 1이다.
    float brdfScale;
#endif
    float3 emission;
};

// view는 vertex에서 path를 거슬러 가는 쪽, 곧 ray가 온 쪽을 향하는 단위 vector다.
PathVertex GetPathVertex(RayHit hit, float3 view)
{
    const HitSurface surface = GetHitSurface(hit);

    PathVertex vertex;
    vertex.position = surface.position;
    vertex.geometryNormal = surface.geometryNormal;
    vertex.normal = GeometricNormals != 0 ? surface.geometryNormal : surface.shadingNormal;
#if LAMBERT_MATERIAL
    // glTF spec과 다르다: Material Lambert는 glTF의 metallic-roughness BRDF 대신 albedo가 baseColor x (1 - metallic)인 Lambert로
    // 반사해서, dielectric의 specular와 metal의 반사가 없다. 반사마다의 f cos / pdf가 albedo라서 furnace의 답을 bit로 알고, 작업 11의
    // DDGI가 계산하는 path와 작업 13의 Cornell box가 같은 BRDF를 쓴다. 정답은 Material glTF로 만든다.
    vertex.albedo = Furnace != 0 ? FurnaceAlbedo : surface.baseColor.rgb * (1.0 - surface.metallic);
#else
    // furnace는 baseColor 1, metallic 0인 BRDF에 Furnace Albedo를 곱한다. 이 BRDF는 모든 view 방향에서 빛을 다 반사하므로
    // (energy-preserving coupling, scripts/check-gltf-brdf.py의 [B5]) directional albedo가 어디서나 Furnace Albedo가 되어, Lambert처럼 답을 식으로 안다. baseColor를 Furnace Albedo로 두면
    // albedo가 1보다 작을 때 specular가 baseColor를 따르지 않아 directional albedo가 view 방향에 따라 달라진다. roughness는 그대로 두어
    // specular lobe도 지나게 한다.
    vertex.brdf = MakeGltfBrdfInput(Furnace != 0 ? 1.0 : surface.baseColor.rgb, Furnace != 0 ? 0.0 : surface.metallic, surface.roughness,
        vertex.normal, view);
    vertex.brdfScale = Furnace != 0 ? FurnaceAlbedo : 1.0;
#endif
    vertex.emission = Furnace != 0 ? FurnaceEmission : surface.emissive;
    return vertex;
}

// directional light의 빛이 vertex에서 한 번 반사해 path를 거슬러 가는 radiance. light는 방향 하나에서만 오므로 반사 방향을 골라서는
// 맞을 수 없어 여기서만 더한다. furnace는 답을 식으로 알도록 light를 끈다.
float3 ReflectDirectionalLight(PathVertex vertex)
{
    if (Furnace != 0 || all(LightIlluminance == 0.0))
        return 0.0;
    RayDesc ray;
    if (!DescribeSurfaceRay(vertex.position, vertex.geometryNormal, LightDirection, ray) || IsOccluded(ray, kCullSingleSidedBackFaces))
        return 0.0;
#if LAMBERT_MATERIAL
    // Lambert의 f는 albedo / π다. illuminance는 light에 수직인 면의 값이므로 normal과의 cos를 곱한다.
    return vertex.albedo / kPi * LightIlluminance * max(0.0, dot(vertex.normal, LightDirection));
#else
    return EvaluateGltfBrdf(vertex.brdf, LightDirection) * vertex.brdfScale * LightIlluminance *
        max(0.0, dot(vertex.normal, LightDirection));
#endif
}

// ray를 따라 camera 쪽으로 오는 radiance를 path 하나로 추정한다.
float3 TraceRadiance(RayDesc ray, inout Random random)
{
    float3 radiance = 0.0;
    float3 throughput = 1.0;
    // bounces는 path가 지금까지 반사한 횟수다. 반사 하나는 그 vertex에서 반사한 directional light와, 다음 hit의 emission이나 sky를 더한다.
    for (uint bounces = 0;; ++bounces)
    {
        RayHit hit;
        if (!TraceClosestHit(ray, kCullSingleSidedBackFaces, hit))
            return radiance + throughput * SkyRadiance;

        const PathVertex vertex = GetPathVertex(hit, -ray.Direction);
        radiance += throughput * vertex.emission;
        if (bounces == MaxBounces)
            return radiance;

        radiance += throughput * ReflectDirectionalLight(vertex);

#if LAMBERT_MATERIAL
        if (!DescribeSurfaceRay(vertex.position, vertex.geometryNormal, SampleLambertDirection(vertex.normal, random.rand2()), ray))
            return radiance;
        // Lambert의 f cos / pdf는 albedo다 (SampleLambertDirection).
        throughput *= vertex.albedo;
#else
        const float lobe = random.rand();
        const GltfBrdfSample reflection = SampleGltfBrdfDirection(vertex.brdf, float3(lobe, random.rand2()));
        if (!DescribeSurfaceRay(vertex.position, vertex.geometryNormal, reflection.direction, ray))
            return radiance;
        throughput *= reflection.weight * vertex.brdfScale;
#endif

        if (RussianRoulette != 0 && bounces + 1 >= kFirstRussianRouletteBounce)
        {
            // 살릴 확률은 1을 넘을 수 없다. 넘는 값으로 나누면 살아남은 path를 덜 키워 bias가 생긴다.
            const float survival = min(1.0, max(throughput.r, max(throughput.g, throughput.b)));
            if (random.rand() >= survival)
                return radiance;
            throughput /= survival;
        }
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
        // 하나라도 더하면 그 뒤로 평균이 NaN이나 Inf에 머문다. 값을 잘라 더하는 것은 bias이므로 버린다.
        if (!all(isfinite(radiance)))
            continue;
        accumulated.a += 1.0;
        accumulated.rgb += (radiance - accumulated.rgb) / accumulated.a;
    }
    Accumulation[pixel] = accumulated;
    SceneColor[pixel] = accumulated.rgb;
}
