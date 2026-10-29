#pragma once

#include <cstdint>

class ComputeContext;
class ColorBuffer;
class RaytracingScene;
class ShadowBuffer;
struct D3D12_VIEWPORT;

namespace Math
{
    class Camera;
    class Matrix4;
    class Vector3;
}

// 다른 기능의 정답 image를 만드는 path tracer. pixel마다 sample을 frame에 걸쳐 누적하고 그 평균을 scene color에 쓴다.
// 누적한 평균이 서로 다른 image의 sample을 섞지 않도록, image를 정하는 조건이 쌓을 때와 달라지면 처음부터 다시 쌓는다.
namespace PathTracer
{
    enum class Output
    {
        kRadiance,
        // DDGI가 같은 시점에서 비교할 irradiance (lux). raster는 처음 맞은 점의 emission과, 그 점이 directional light에서 바로 받는 빛을
        // 직접 구하므로 이 둘을 뺀다. 그 점의 BRDF도 raster가 곱하므로 곱하지 않는다. pixel 중심을 쏘는 것은 TAA를 끈 raster가 그 점에서
        // shading하기 때문이다.
        // Shading DDGI Probe Hit로 쌓은 값은 DDGI와 irradiance를 구하는 방식 (probe의 보간과 path tracing)만 다르다. 그래서 DDGI와
        // Probe Hit 값의 차는 DDGI의 irradiance 근사의 오차이고, Probe Hit 값과 Shading glTF 값의 차는 probe hit의 shading이 정답과 다른
        // 몫이다.
        kIndirectIrradiance,
    };

    // path의 vertex를 shading하는 방식. Shading마다 따로 compile한 shader로 그린다 (PathTracerCS.hlsl의 LAMBERT_SHADING).
    enum class Shading
    {
        // glTF의 metallic-roughness BRDF (Core/Shaders/GltfBrdf.hlsli). 정답은 이것으로 만든다.
        kGltf,
        // albedo가 baseColor x (1 - metallic)인 Lambert. 반사마다의 f cos / pdf가 albedo라서 furnace의 답을 bit로 알 수 있다.
        kLambert,
        // DDGI가 probe ray가 맞은 점 (probe hit)을 shading하는 방식 (Core/Shaders/SurfaceShading.hlsli). 정해진 mip의 texture,
        // glossy 반사를 합친 albedo의 Lambert, raster와 같은 shadow map으로 shading한다. Output Radiance에서는 camera ray가 맞은 점부터
        // 모든 vertex가 probe hit이므로, camera 위치의 probe가 보는 radiance가 된다. Output Indirect Irradiance에서는 처음 맞은 점을
        // shading하지 않고, 처음 맞은 점 뒤의 vertex가 probe hit다. 정답을 만드는 glTF와 Lambert는 shadow ray와 mip 0을 쓴다.
        kProbeHit,
    };

    struct Settings
    {
        Output output = Output::kRadiance;
        Shading shading = Shading::kGltf;
        // pixel마다 한 frame에 더하는 sample 수
        uint32_t samplesPerFrame = 1;
        // 이만큼 쌓으면 trace를 멈추고 평균만 보인다. 기본값은 정답의 spp로, scripts/check-convergence.py의 [C2]가 정한다.
        uint32_t targetSamples = 131072;
        // seed만 다른 두 누적은 같은 image의 서로 다른 추정이다.
        uint32_t seed = 0;
        // path가 반사하는 횟수의 상한. 0이면 처음 맞은 곳의 emission과 camera에서 바로 보이는 sky만 보고, 1이면 처음 맞은 곳에서 반사한
        // directional light와 그곳에서 한 번 반사해 본 것을 더한다.
        uint32_t maxBounces = 64;
        // PathTracerCS.hlsl의 kFirstRussianRouletteBounce번째 반사부터 throughput에 따라 path를 끊고 살아남은 path를 그만큼 키운다.
        // 기대값은 그대로다.
        bool russianRoulette = true;

        // shading normal 대신 geometric normal로 반사한다. shading normal은 README.md의 "camera 밖에서 본 doubleSided" 규칙 때문에
        // geometric normal 아래로 고른 방향의 energy를 잃으므로, furnace의 답을 그 손실 없이 보려고 쓴다.
        bool geometricNormals = false;
        // 모든 surface의 albedo와 emission을 아래 값으로 두고 directional light를 끈다. sky light는 모든 방향에서 같으므로 답을 식으로 아는 장면이 된다.
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
    // skyRadiance는 ray가 아무 면도 만나지 않을 때 받는 sky light의 radiance (cd/m²)이고 모든 방향에서 같다.
    // shadowMap은 raster가 이번 frame에 그린 directional light의 shadow map이고, shadowMatrix는 world position을 그 uv와 depth로 옮긴다.
    // Shading DDGI Probe Hit만 둘을 읽는다.
    void Render(ComputeContext& context, const Settings& settings, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        const RaytracingScene& scene, const Math::Vector3& lightDirection, const Math::Vector3& lightIlluminance, const Math::Vector3& skyRadiance,
        ShadowBuffer& shadowMap, const Math::Matrix4& shadowMatrix, ColorBuffer& sceneColor);

    // harness의 capture pt에 지금까지 누적한 buffer로 답한다. Render를 부르지 않는 frame에도 불러서, 누적을 멈춘 동안에도 답하게 한다.
    void Capture(ComputeContext& context);
}
