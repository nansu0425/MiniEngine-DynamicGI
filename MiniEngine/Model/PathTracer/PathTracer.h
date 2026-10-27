#pragma once

#include <cstdint>

class ComputeContext;
class ColorBuffer;
class RaytracingScene;
struct D3D12_VIEWPORT;

namespace Math
{
    class Camera;
    class Vector3;
}

// 다른 기능의 정답 image를 만드는 path tracer. pixel마다 sample을 frame에 걸쳐 누적하고 그 평균을 scene color에 쓴다.
// 누적한 평균이 서로 다른 image의 sample을 섞지 않도록, image를 정하는 조건이 쌓을 때와 달라지면 처음부터 다시 쌓는다.
namespace PathTracer
{
    // 반사의 BRDF. Material마다 따로 compile한 shader로 그린다 (PathTracerCS.hlsl의 LAMBERT_MATERIAL).
    enum class Material
    {
        // glTF의 metallic-roughness BRDF (Core/Shaders/GltfBrdf.hlsli). 정답은 이것으로 만든다.
        kGltf,
        // albedo가 baseColor x (1 - metallic)인 Lambert. 반사마다의 f cos / pdf가 albedo라서 furnace의 답을 bit로 알 수 있다.
        kLambert,
    };

    struct Settings
    {
        Material material = Material::kGltf;
        // pixel마다 한 frame에 더하는 sample 수
        uint32_t samplesPerFrame = 1;
        // 이만큼 쌓으면 trace를 멈추고 평균만 보인다.
        uint32_t targetSamples = 4096;
        // seed만 다른 두 누적은 같은 image의 서로 다른 추정이다.
        uint32_t seed = 0;
        // path가 반사하는 횟수의 상한. 0이면 처음 맞은 곳의 emission과 camera에서 바로 보이는 sky만 보고, 1이면 처음 맞은 곳에서 반사한
        // directional light와 그곳에서 한 번 반사해 본 것을 더한다.
        uint32_t maxBounces = 64;
        // PathTracerCS.hlsl의 kFirstRussianRouletteBounce번째 반사부터 throughput에 따라 path를 끊고 살아남은 path를 그만큼 키운다.
        // 기대값은 그대로다.
        bool russianRoulette = true;
        // 모든 방향에서 같은 sky의 radiance. glTF의 emissive처럼 cd/m²로 읽는다.
        float skyRadiance = 1.0f;

        // shading normal 대신 geometric normal로 반사한다. shading normal은 README.md의 "camera 밖에서 본 doubleSided" 규칙 때문에
        // geometric normal 아래로 고른 방향의 energy를 잃으므로, furnace의 답을 그 손실 없이 보려고 쓴다.
        bool geometricNormals = false;
        // 모든 surface의 albedo와 emission을 아래 값으로 두고 directional light를 끈다. sky도 상수이므로 답을 식으로 아는 장면이 된다.
        // glTF에서는 baseColor 1, metallic 0, material의 roughness인 BRDF에 albedo를 곱해, 모든 view 방향에서 directional albedo가 albedo다.
        bool furnace = false;
        float furnaceAlbedo = 1.0f;
        float furnaceEmission = 0.0f;
    };

    struct Progress
    {
        // reset 뒤 pixel마다 dispatch한 sample 수
        uint32_t samples;
        // reset 뒤 sample을 더한 frame들의 시간
        double seconds;
        // 실행 뒤 처음부터 다시 쌓기 시작한 횟수. 첫 누적도 센다.
        uint32_t resets;
    };

    void Initialize(void);
    void Shutdown(void);

    // 다음 Render가 처음부터 다시 쌓는다.
    void Reset(void);
    Progress GetProgress(void);

    // camera ray는 rasterViewport로 그린 raster의 pixel 격자를 따른다. sceneColor는 raster가 그린 scene이고, 그 크기로 누적해 그 위에 평균을 쓴다.
    // lightDirection은 surface에서 directional light를 향하는 단위 vector이고, lightIlluminance는 light에 수직인 면의 illuminance (lux)다.
    void Render(ComputeContext& context, const Settings& settings, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        const RaytracingScene& scene, const Math::Vector3& lightDirection, const Math::Vector3& lightIlluminance, ColorBuffer& sceneColor);

    // harness의 capture pt에 지금까지 누적한 buffer로 답한다. Render를 부르지 않는 frame에도 불러서, 누적을 멈춘 동안에도 답하게 한다.
    void Capture(ComputeContext& context);
}
