# Reference Path Tracer 로드맵

DDGI 와 RT Reflection 의 정답 image 를 만드는 path tracer 를 inline RayQuery compute pass 로 만든다. 작업 branch 는 `feat/reference-path-tracer` 이고, 아래 작업을 끝내면 `main` 에 merge 한다. 10/30 WEEK 3 에 발표하고, 그 전에 끝낼 작업과 기한은 아래 일정에 있다. PT 와 PT 가 쓰는 engine 코드는 README "Rendering 기준" 대로 spec 의 MUST 와 SHOULD 를 모두 따른다. DDGI 와 PT 의 비교 수치는 외부 renderer 와 맞춘 작업 13 이 통과한 뒤에 믿는다.

## 목표

- glTF metallic-roughness material 을 bias 없이 path tracing 해서 화면의 radiance 와 primary hit 의 간접 irradiance 의 정답을 만든다. spec 은 physically realistic BRDF 로 한 unbiased simulation 을 정답으로 본다 (`Specification.adoc:3657`).
- 맞게 계산했는지를 분산 0 인 furnace, BRDF 단위의 test, 독립 누적 두 개의 relative RMSE, 외부 renderer 로 그린 Cornell box 로 보인다.
- DDGI 의 오차를 "DDGI 가 계산하지 않는 path" 와 "계산하는 path 의 정확도" 로 나눌 수 있게 비교할 양을 정의한다.
- 측정 조건을 Log 한 줄로 되돌리고 float image 로 export 해서, DDGI 가 같은 시점에서 같은 양과 비교하게 한다.

## 전제

- 장면은 `EditorCamera` 로 연 정적 Sponza 다. Restore 는 EditorCamera 에서만 된다 (`ModelViewer.cpp:384-394`). animation 과 instance 변경은 reset 조건에 넣지 않는다.
- Sponza 는 작업 B 부터 KhronosGroup glTF-Sample-Assets `8f49239` 의 `Models/Sponza/glTF/` (이하 Khronos 판) 다. 이 폴더가 마지막으로 바뀐 commit 은 2023-05-18 의 `5bad5aa` 다. 그 전에는 DirectX-Graphics-Samples `e5975f9` 의 `MiniEngine/ModelViewer/Sponza/pbr/sponza2.gltf` (이하 옛 판) 였고, 옛 판은 같이 두지 않는다.
  - 고른 이유: 형식과 규약이 spec 을 따르는 공개 asset 이라, spec 을 따르는 renderer 끼리, 외부 renderer 까지 같은 입력으로 비교할 수 있다. KhronosGroup glTF-Validator 2.0.0-dev.3.10 에서 Khronos 판은 error, warning, info 가 모두 0 이다. 옛 판은 error 6184, warning 1928 이고, 대부분 accessor min/max 의 반올림과 byteStride 문제라 해석은 바뀌지 않는다.
  - geometry: triangle 262,267 개와 축 (+Y 가 위), UV 가 옛 판과 같다. node scale 이 0.008 이라 장면이 옛 판의 0.8 배이므로, 작업 B 가 `Resize` 의 ×100 을 ×125 로 바꿔 world 좌표를 옛 판과 같게 했다 (125 × 0.0080000004 ≈ 1). 옛 판에서 정한 시점은 그대로다.
  - TANGENT: normal map 을 쓰는 primitive 102 개 모두에 있다 (103 개 중 없는 1 개는 normal map 이 없다). Khronos 판의 README 는 MikkTSpace 로 만들었다고 한다. T 가 +u 방향인 비율이 99.8% 이상, `cross(N, T) · w` 가 image 위쪽인 비율이 99.9% 이상, w 와 UV handedness 의 일치가 100% 다 (측정). 그래서 Sponza 의 normal map 은 모두 asset 의 TANGENT 로 읽힌다 (작업 4).
  - texture: base color 등 65 장이 JPEG (q≈95, ICC 와 EXIF 없음) 이고 1024² 다. 옛 판에서 2048² 였던 curtain 은 절반이 되었다.
- asset 의 값은 공개된 그대로 쓰고 고치지 않는다. 아래는 Khronos 판의 알려진 결함이다. material 값은 Pestana (2015) 의 programmer art 이고 base color 는 2010 년 Crytek 의 diffuse 다.
  - normal map 24 장 (내용으로 20 장) 중 22 장이 glTF 규약이다. leaf (thorn) 는 DirectX 규약이라 green 이 뒤집혀 보이고, chain 은 판정할 수 없다 (측정).
  - 모든 material 의 baseColorFactor 가 0.588 이라 dielectric albedo 가 0.59 배가 된다. Khronos 판의 README 도 "I assume" 라고만 적어 의도를 알 수 없다.
  - flagpole, leaf (thorn), vase_plant 의 roughness 가 specular 와 양의 상관이다. leaf 면적의 19.7% 가 roughness < 0.1 이다 (측정).
  - chain, curtain, flagpole, vase_hanging 의 금속 영역은 base color 가 매우 어둡다 (측정).
- 기본 Environment 는 `FindFirstFile` 이 처음 찾은 set 이다 (`ModelViewer.cpp:290-331`). NTFS 는 이름순으로 돌려주므로 Atrium 이다 (추측, Windows 는 순서를 보장하지 않는다). cubemap 에 sun disk 는 없다. +Y 면에 큰 천창이 있고 (8 bit 로 decode 하면 texel 의 39% 가 1 을 넘는다) 작은 downlight 와 간판 조명이 있다. 그 HDR 밝기는 재지 않았다.
  - CloudCommon 은 목록에 없다. DirectX-Graphics-Samples `e5975f9` 에서 받은 `Textures/` 의 짝이 `CloudCommon_diffuseIBL.dds` 와 `CloudCommons_specularIBL.dds` 로 이름이 어긋나, 찾는 `CloudCommon_specularIBL.dds` 가 invalid 라 그 set 을 넣지 않는다 (`ModelViewer.cpp:310-320`, `TextureManager.cpp:129-131`). harness log 도 "Found 5 IBL environment map sets" 다. upstream asset 의 이름 문제라 고치지 않는다.
- 입력이 없으면 EditorCamera 의 pose 가 frame 마다 bit 단위로 같다 (작업 3 에서 확인). 같은 GPU 와 driver 에서 같은 입력은 같은 bit 의 결과를 낸다 (추측, 작업 6 에서 확인).
- `SurfaceRayOrigin` 의 offset 이 Sponza ×125 (Khronos 판. world 좌표는 옛 판 ×100 과 같다) 에서 충분한지는 재지 않았다 (323e185). PT 에서 self-hit 나 뚫림으로 보이는 문제가 생기면 그때 검증 pass 에 mode 를 더한다. furnace 는 이 문제를 잡지 못하고 Cornell box (작업 13) 가 잡는다.
- PT 가 쓰는 입력 경로 (loader → material data → `HitSurface`) 와 단위, light 가 spec 과 다른 곳은 설계할 때 조사했다. README "Rendering 기준" 대로 그 입력을 처음 쓰는 작업이 고친다. 작업마다 "입력 경로" 항목에 적었다.
- GPU 는 RTX 4060 Laptop 이다 (ef16b70). 1 spp 의 비용은 아직 모르고 작업 5 에서 처음 잰다.

## 설계

### 출력

Output 설정으로 둘 중 하나를 누적한다. pixel 격자는 `CameraRays.h:10-11` 의 격자 (TAA 를 끈 raster 의 pixel) 다. Radiance 는 cd/m², Indirect Irradiance 는 lux 다 (아래 Light).

- **Radiance**: pixel (x, y) 의 `[x, x+1) × [y, y+1)` 에서 고르게 고른 위치로 camera ray 를 쏴서, camera 에 들어오는 radiance 를 평균한다 (box filter). miss 하면 sky radiance 다.
- **Indirect Irradiance**: pixel 중심 `(x + 0.5, y + 0.5)` 을 지나는 camera ray 의 closest hit x₁ 에서 `E(x₁) = ∫ L_i(x₁, ω) max(0, n_s·ω) [n_g·ω > 0] dω` 를 구한다.
  - n_s, n_g 는 `HitSurface` 의 shading normal 과 geometric normal 이다. `[n_g·ω > 0]` 은 README "camera 밖에서 본 doubleSided" 의 규칙이다.
  - L_i 는 x₁ 에서 ω 쪽으로 처음 보이는 surface 나 sky 의 radiance 다. sun 의 직접광만 빠지고, 보이는 sky 와 다른 surface 가 내보내는 빛 (그 surface 의 직접광, 간접광, emission) 은 들어간다. x₁ 의 BRDF 와 emission 은 들어가지 않는다.
  - pixel 중심은 TAA 를 끈 raster 의 sample 위치 (`CameraRays.h:19-21`) 다. DDGI 는 raster 가 그 pixel 에서 쓰는 점과 n_s 로 같은 양을 구해 pixel 마다 비교한다. 그래서 비교할 때는 TAA 를 끈다.
  - primary ray 가 miss 한 pixel 은 값이 없다 (export 의 sample 수 0). 화면에는 E/π 를 보인다. 흰 Lambert 가 그 irradiance 로 내는 radiance 라 Radiance 와 밝기가 비슷하다.
- **DDGI 가 계산하는 path**: Indirect Irradiance 를 Material 을 Lambert 로 두고 누적한 것이다. x₁ 에서 모으는 방법은 같고, x₂ 부터의 모든 반사가 DDGI 의 probe trace 와 같은 Lambert (`ρ_d = baseColor × (1 − metallic)`) 다. 그러면 DDGI 와 PT 의 차이가 `(DDGI − DDGI 경로) + (DDGI 경로 − 전체)` 로 나뉜다. 앞 항은 DDGI 가 계산하는 path 의 정확도 (probe 해상도, leak, hysteresis) 이고, 뒤 항은 DDGI 가 계산하지 않는 path (x₂ 이후의 specular 와 metal 반사, Fresnel coupling) 의 몫이다. DDGI 의 probe trace 는 이 Lambert 를 같은 코드로 쓴다.
- pixel 넓이 평균이나 sky 를 뺀 bounce 빛은 비교할 양으로 고르지 않았다. 앞은 edge 에서 DDGI 가 한 점에서 구한 값과 달라지고, 뒤는 probe 가 sky 를 포함해 모으므로 DDGI 쪽에서 sky 를 따로 빼야 한다. 최종 radiance 로 비교하는 방법도 고르지 않았다. raster 의 BRDF 가 spec 과 달라서 DDGI 를 넣은 raster 화면이 PT 와 같은 양이 되지 않는다 (8432ed7).
- DDGI 는 `[n_g·ω > 0]` 을 모르므로, n_s 와 n_g 가 δ 만큼 다른 곳에서 반사마다 (1 − cos δ)/2 (계산) 가 DDGI 경로와의 차이에 섞인다. 그 크기는 작업 6 의 furnace 로 본다.

### BRDF

- 구조는 spec 의 normative 규칙대로다: `material = mix(dielectric, metal, metallic)` (`Specification.adoc:3532`), dielectric 은 ior 1.5 의 `fresnel_mix(diffuse, specular)`, metal 은 `conductor_fresnel(baseColor, specular)` (`:3564-3598`, "Complete Model"). 두 specular 는 D, V 가 같아서 `f0 = mix(0.04, baseColor, metallic)` 인 lobe 하나로 합친다 (Schlick 이 f0 에 선형이다).
- lobe 식은 sample implementation (informative, `:3660` 부터) 을 쓴다. GGX D, height-correlated Smith V (`:3725-3728`), `|V·H|` 의 Schlick F, `α = roughness²` 다. α 는 `α_min = 1e-3` 아래로 내리지 않는다 (spec 이 clamp 를 권한다, `:3741`). fp32 의 D 최대값 `1/(π α²) ≈ 3.2e5` 가 안전한 범위다. Sponza 에서 roughness 가 0.1 보다 작은 texel 은 2.4% 이고 flagpole, thorn (20% 이상) 과 vase_plant 에 몰려 있다 (옛 판의 metallicRoughness PNG 의 G 를 잼. 이 texture 는 linear 로 남아 있다). Khronos 판의 G 는 옛 판과 상관이 0.998 ~ 1.000 이라 비율도 거의 같다고 본다 (추측).
- diffuse 와 specular 의 coupling 은 Kulla & Conty 2017 의 energy-preserving 형태 `f_d = ρ (1 − E_s(μ_o)) (1 − E_s(μ_i)) / (π (1 − Ē_s))` 로 sample 의 `fresnel_mix` 식을 대신한다. E_s(μ, roughness) 는 f0 = 0.04 인 Fresnel-weighted specular 의 directional albedo 이고 Ē_s 는 그 cosine 평균이다. ior 이 1.5 로 고정이라 spec 이 말한 16³ 대신 2D table (32 × 32) 과 1D table 이면 된다. table 은 script 가 같은 lobe 식으로 만든다.
  - spec 은 sample 의 `fresnel_mix` 가 energy conservation 을 깬다고 적고 (`:3877`), physically accurate simulation 의 BRDF 는 positive, reciprocal, energy conserving 이어야 한다 (MUST, `:3653-3655`).
  - quadrature 로 잰 directional albedo (baseColor 1, metallic 0, roughness 0.3 ~ 1.0, 이 문서를 쓸 때 계산): `fresnel_mix` 는 1.27 까지 올라간다. `(1 − F(N·L))(1 − F(N·V))` (`:3879`) 는 energy 를 지키지만 수직에서 0.89 ~ 0.92, μ_o = 0.05 에서 0.26 ~ 0.53 으로 어두워진다. Kulla & Conty 는 1.0000 이다.
  - `(1 − F)(1 − F)` 는 고르지 않았다. table 이 필요 없지만 x₂ 이후의 diffuse 를 수직에서 약 8% 어둡게 해서 DDGI 의 정답이 그만큼 바뀌고, Burley 는 그 grazing 의 어두워짐을 측정에서 보지 못했다 (`:3879`). Jakob 2014 는 spec 이 무겁다고 한다 (`:3881`).
  - metal 은 single scattering 이라 rough 한 metal 이 energy 를 잃는다 (roughness 1 수직에서 albedo 0.31, 계산). spec 은 multiple scattering 을 권하지만 (`:3637`) 일정 때문에 뒤로 미룬다.
- 교체 단위는 f·cos 평가, 방향 sampling (방향, f·cos/pdf, pdf), pdf 평가 세 함수다. `GltfBrdf.hlsli` 와 `LambertBrdf.hlsli` 가 같은 함수를 구현하고 Material 설정이 고른다.
- Lambert 는 정답이 아니다. glTF 의 dielectric 은 늘 0.04 의 specular 를 가지므로 순수 Lambert 를 표현하지 못한다. 그래서 Lambert 는 두 곳에만 쓰고 `glTF spec과 다르다` 주석을 단다.
  - DDGI 가 계산하는 path 의 정의 (`ρ_d = baseColor × (1 − metallic)`)
  - 검증용 override: 분산 0 인 furnace (ρ 상수) 와 Cornell box 비교 (작업 13)

### Light

- 단위: scene radiance 1.0 은 1 cd/m² 다. emissive 의 단위 (`Specification.adoc:2691`) 에 맞추고, lux 와 같이 쓰면 Lambert 반사가 `L = ρE/π` 로 같은 단위가 된다. 정의와 이유는 light hlsli 한 곳에 적는다.
- Sun: `GetSunDirection()` (`ModelViewer.cpp:105-112`) 쪽의 delta directional light 다. `Sun Light Intensity` 를 KHR_lights_punctual 의 directional light 처럼 sun 에 수직인 면의 illuminance (lux) 로 읽는다. 기본값은 4π 다 (작업 7). `E_sun = π × Intensity` 로 raster 의 지금 식에 맞추는 방법은 고르지 않았다. app 의 sun 이 glTF 의 directional light 와 π 배 다른 단위가 된다.
- Sky: 고른 Environment 의 radiance cube (`*_specularIBL.dds`) mip 0 을 world 방향으로 bilinear sampling 한다. skybox 와 같은 방향 규약이다 (`SkyboxVS.hlsl:33-37`). cube 에는 단위가 없으므로 cd/m² 로 읽는다 (app 의 정의). Environment 가 None 이면 0 이다.
  - skybox 가 보이는 Gloss Reduction mip (기본 2, `Renderer.cpp:462-466`) 은 고르지 않았다. 정답이 raster 의 specular 설정에 묶이고 흐린 sky 가 정답이 된다. mip 0 이 원래의 radiance 다. Stonewall (fp16) 의 radiance mip 0 을 cosine 적분하면 irradiance cube 와 rgb 54개 중 51개가 0.91 ~ 1.06 배로 맞았다 (`π × cube / E`, 계산). 그래서 irradiance cube 는 E/π 를 담고, raster 의 diffuse IBL 도 이 단위에서 Lambert 와 맞는다.
- Emission: `HitSurface.emissive` (`HitSurface.hlsli:62`) 를 cd/m² 의 radiance 로 더한다. ray 가 맞을 수 있는 면은 culling 규칙이 정하므로 맞은 면이 방출한다. Sponza 에는 emissive 를 쓰는 material 이 없다 (Khronos 판의 `Sponza.gltf` 에 `emissive` 가 없다, 측정).

### Path 와 sampling

- closest hit 와 visibility 는 `TraceClosestHit`, `IsOccluded` 를 `kCullSingleSidedBackFaces` 로 쓴다 (README "camera 밖에서 본 doubleSided"). surface 에서 나가는 ray (sun, 다음 반사) 는 `DescribeSunRay` (`RaytracingDebugViewCS.hlsl:92-99`) 를 방향을 받는 공용 함수로 옮겨 debug view 와 같이 쓴다. n_g 가 그 방향을 등지면 쏘지 않고 가려진 것으로 친다.
- 다음 방향은 BRDF sampling 으로만 고른다. lobe 는 `P_s = Y(F(f0, μ_o)) / (Y(F(f0, μ_o)) + (1 − metallic) Y(baseColor) (1 − E_s(μ_o)))` (Y 는 luminance) 로 고른다. specular 는 Capsaicin 의 bounded VNDF (`references/Capsaicin/src/core/src/materials/material_sampling.hlsl:142-168`, pdf `:231-249`) 를 AMD 의 MIT 문구와 함께 가져와 쓰고, diffuse 는 n_s 둘레의 cosine hemisphere 다. weight 는 두 lobe 를 더한 f 와 mixture pdf `P_s pdf_s + (1 − P_s) pdf_d` 로 낸다.
  - P_s 는 view 와 material 만으로 정해지므로 어떤 방향의 pdf 도 다시 구할 수 있다. Capsaicin 의 근사 specular 방향 (`material_sampling.hlsl:280-336`) 은 같은 성질을 가지지만 코드가 더 길어 고르지 않았다.
  - Capsaicin 의 pdf 가 부르는 D 는 분모에만 eps 를 더해 정규화가 깨지므로 (`material_evaluation.hlsl:75-87`) 이 repo 의 정규화된 D 로 바꾼다.
  - n_s·V < 0 이면 sample implementation 처럼 `|N·V|` 로 평가하고 bounded VNDF 가 그 경우를 다룬다 (Tokuyoshi 2021). cos 항은 `max(0, n_s·L)` 이다.
- MIS 는 필요 없다. sun 은 delta 라 BRDF sampling 으로 맞지 않으므로 NEE 로만 더하고, sky 와 emission 은 BRDF sampling 으로만 맞으므로 전략이 하나다. sky 의 importance sampling 과 MIS 는 Atrium 의 천창이 firefly 를 만들 때 더한다 (뒤로 미룬 일).
- Max Bounces B 는 반사 횟수의 상한이다. k 번 반사한 빛 (x_k 의 sun NEE, x_k 에서 반사해 본 sky 와 x_{k+1} 의 emission) 을 k ≤ B 일 때 더한다. B = 1 은 직접광만이다. 기본값은 64 다. Indirect Irradiance 는 x₁ 에서 모으는 것을 반사 하나로 세므로, B = 2 가 hit 의 직접광만 모으는 DDGI single bounce 와 같은 빛이다.
- Russian roulette 는 3번째 반사부터 `q = min(1, max(β.r, β.g, β.b))` 로 살리고 β 를 q 로 나눈다. β 는 x₁ 이후 weight 의 곱이다 (Indirect Irradiance 의 π 는 넣지 않는다). Capsaicin 의 식은 clamp 하지 않은 값으로 나눠 bias 가 생기므로 따르지 않는다 (`references/Capsaicin/src/core/src/ray_tracing/path_tracing.hlsl:449-458`). glTF BRDF 에서는 weight 가 1 을 넘을 수 있어 min 이 필요하다. 설정으로 끌 수 있다.
- sample 하나의 결과가 NaN, Inf, 음수면 누적하지 않는다. 그 pixel 의 sample 수가 오르지 않으므로 export 에서 센다. firefly clamp 는 bias 이므로 하지 않는다.
- texture 는 지금처럼 mip 0 을 읽는다 (`RaytracingScene.hlsli:139-145`). pixel 안을 고르게 누적하므로 supersampling 이 된다 (추측).
- RNG 는 Capsaicin 의 PCG (`references/Capsaicin/src/core/src/components/random_number_generator/random_number_generator.hlsl:38-67`) 를 MIT 문구와 함께 `Core/Shaders/Random.hlsli` 로 가져온다. CPU seed buffer 를 읽는 `MakeRandom` 대신 Seed 설정, pixel, sample 번호를 PCG hash 로 섞어 path 마다 state 하나를 만들고, 고쳤다고 적는다. sample 번호는 reset 뒤 dispatch 한 sample 의 순번이다. 독립 누적 두 개는 Seed 만 다르게 한다. Sobol 같은 low-discrepancy sequence 는 수렴 속도만 바꾸므로 고르지 않았다.

### 누적과 표시

- 누적 buffer 는 scene 크기의 R32G32B32A32_FLOAT 다. rgb 는 running mean, a 는 누적한 sample 수다. reset 뒤 첫 sample 은 shader 가 덮어쓴다. `RaytracingScene::Bind` 뒤에는 dynamic descriptor 를 쓸 수 없어 ClearUAV 를 쓰지 않는다 (`RaytracingScene.h:26-29`).
- 변경 알림이 없으므로 아래 값을 앞 frame 과 비교해 다르면 reset 한다.
  - `DescribeCameraRays` 의 결과에서 `rasterSampleOffset` 을 뺀 것. camera 위치, 방향, FOV, 해상도가 들어가고 TAA jitter 는 빠진다.
  - sun 방향과 intensity, sky cubemap resource, Output, Material, Max Bounces, Russian Roulette, Seed, light 와 검증 설정, Reset button
  - reset 하지 않는 것: Samples per Frame, Target Samples, exposure, bloom, tone mapping, TAA, Show
- Target Samples 에 이르면 trace 를 멈추고 평균만 표시한다. 기본값은 작업 12 가 정하고, 그 전에는 4096 이다.
- 평균을 `ModelViewer::RenderScene` 의 마지막 pass (TAA resolve, DoF 또는 motion blur, `ModelViewer.cpp:563-571`) 뒤에 `g_SceneColorBuffer` 에 쓴다. PT 가 raster 와 같은 bloom, exposure, tone mapping 을 거쳐 두 screenshot 을 같은 조건으로 나란히 볼 수 있다. raster 는 계속 그린다 (validation view 가 raster depth 를 읽는다). ray tracing view 는 지금처럼 overlay 로 PT 위에 그린다.
- debug view 처럼 overlay 에 자체 tone mapping 으로 그리는 방법은 고르지 않았다. raster 와 tone mapping 이 달라 나란히 보기 어렵고 overlay 는 R8G8B8A8_UNORM 이다. screenshot 을 비교할 때는 Adaptation 을 끄고 Exposure 를 고정한다. export 는 tone mapping 전의 누적값이다.

### UI

`RaytracingPanel.h:5` 에 따라 "Path Tracer" panel 을 따로 `ImGuiLayer::ShowPanel` 로 만들고, 설정은 editor.ini 의 "Path Tracer" section 에 저장한다.

- 설정: Show (끄면 누적을 멈추고 내용은 남긴다), Output, Material (glTF, Lambert), Max Bounces, Russian Roulette, Samples per Frame, Target Samples, Seed, Reset
- light: Sun, Sky, Emission
- 검증: Furnace (모든 surface 의 albedo 와 emission, sky radiance 를 상수로 두고 sun 을 끈다. glTF 에서는 baseColor 를 albedo 로, metallic 을 0 으로 둔다), Geometric Normals (n_s 대신 n_g), BRDF Test
- 진행: sample 수와 Target, reset 뒤 걸린 시간, sample/s. pass 의 GPU time 은 Profiler panel 의 "Path Tracer" 줄에서 본다 (`EngineProfiling.cpp:290-323`, Profile build).
- Capture: Export, Export at Powers of 4, Log, Restore (작업 10)
- override 는 surface 와 sky 의 입력값만 바꾸고 path loop 는 그대로 둔다. loop 안에 따로 분기를 두면 검사하려는 코드를 건너뛴다.

### Export 와 비교

- Export 는 GPU 를 기다려 누적 buffer 를 DDS (R32G32B32A32_FLOAT) 로 쓴다. DirectXTex 는 Model 이 이미 쓴다 (`TextureConvert.cpp:19, 261`). 위치는 working directory 의 `Captures/` (`MiniEngine/ModelViewer/Captures/`) 이고 `.gitignore` 에 더한다. 파일 이름에 Output, Material, Seed, sample 수를 넣고 옆에 Log 한 줄을 `.txt` 로 둔다. 1804 × 1462 에서 한 파일이 42 MB 다.
- invalid sample 수는 dispatch 한 sample 수와 a 의 차이를 더한 것이다. Indirect Irradiance 에서는 a = 0 인 pixel (miss) 을 뺀다.
- 비교는 `scripts/compare-images.py` (Python 3, numpy, matplotlib, Pillow) 가 한다. 이 PC 에는 Python 3.12.10, numpy 2.4.4, matplotlib 3.10.9, Pillow 12.2.0 이 있다. PowerShell 은 수백만 pixel 의 배열 연산과 그래프에 맞지 않아 고르지 않았다.
  - `diff`: `rRMSE(X, R) = sqrt(Σ (X − R)² / Σ R²)`. 합은 두 image 모두 sample 이 있는 pixel 의 rgb 전체다. 어두운 pixel 이 값을 키우지 않는다. heatmap 은 pixel 마다 `|Y(X) − Y(R)| / max(Y(R), 0.01 × mean Y(R))` 를 log scale 로 칠한 PNG 다.
  - `furnace`: 기대값 image 대비 `X / expected − 1` 의 mean, min, max 와 |·| 가 1e-4 를 넘는 pixel 비율, invalid sample 수. 기대값은 상수나 아래의 항등식으로 준다.
  - `agree`: 같은 spp 의 네 run (설정 A 와 B 를 Seed 0, 1 로) 에서 `r = rRMSE(A₀, B₁) / sqrt((rRMSE(A₀, A₁)² + rRMSE(B₀, B₁)²) / 2)` 를 낸다. 두 설정이 같은 기대값의 독립 추정이면 분자의 제곱은 두 분산의 합이고, 분모의 각 항은 같은 설정 두 run 의 차라 그 분산의 두 배다. 그래서 bias 가 없으면 r ≈ 1 이고 bias b 가 있으면 `r² ≈ 1 + b²/noise²` 다. RR 은 unbiased 여도 분산을 키우므로, RR 을 켠 두 run 의 rRMSE 하나와 바로 비교하면 맞는 구현도 실패할 수 있다.
  - `convergence`: Seed 가 다른 두 Powers of 4 export 를 sample 수끼리 diff 해 표, log-log 그래프, 기울기를 낸다
  - `png`: 고정 exposure 로 sRGB PNG 를 만든다 (발표용). `brdf`: BRDF Test 의 CSV 로 directional albedo 그래프를 만든다

### 재현

- PT 의 Log 는 장면 조건, 해상도, TAA, exposure, PT 설정, sample 수, 걸린 시간, invalid sample 수를 검증의 Log 와 같은 형식으로 한 줄에 찍는다. exposure 는 screenshot 의 조건으로 기록만 한다.
- 작업 10 이 장면 조건 (`RayQueryValidation.h` 의 `SceneConditions`, `RayQueryValidation.cpp:302-363` 의 줄 형식과 parse) 을 `Core/Editor/SceneConditions` 로 옮겨 검증과 PT 가 같이 쓰고, sun intensity 와 environment 를 Engine Tuning 의 `ToString`, `SetFromString` 으로 더한다. PT 의 Restore 는 장면 조건과 PT 설정을 되돌리고 해상도가 다르면 경고한다.
- 작업 10 을 빼야 하면 (아래 일정) PT 의 Log 줄을 검증 panel 의 Restore 에 붙여 camera, sun 각도, TAA 를 되돌린다. parse 는 prefix 가 달라도 key 를 읽는다 (`RayQueryValidation.cpp:348-363`). sun intensity 와 environment 는 줄을 보고 손으로 맞춘다.

### raster 와 나란히 볼 때

raster 는 기준이 아니다 (8432ed7). 같은 시점의 두 화면으로 볼 수 있는 것은 그림자 위치, sky 의 방향, texture 배치, GI 가 더한 빛의 분포다. 작업 1, 7 뒤에도 raster 에는 Burley factor, `c_diff` 의 0.96 (`DefaultPS.hlsl:269`), Hable 의 visibility 근사, split-sum 이 아닌 specular IBL, shadow map 의 PCF, SSAO, 가려지지 않는 diffuse IBL 이 남는다. 그래서 pixel 값의 차이가 PT 의 오류를 뜻하지 않고, raster 와의 수치 기준은 두지 않는다.

## 작업

완료 기준의 수치와 Log 줄은 그 작업을 담은 commit 의 message 에 남기고, roadmap 보다 오래 남아야 하는 이유는 구현할 때 코드 주석으로 남긴다. "입력 경로" 는 그 작업이 처음 쓰는 입력이 spec 과 다른 곳을 다루는 자리다 (README "Rendering 기준"). 기한과 순서의 이유는 아래 일정에 있다. 작업 1 뒤에 더한 두 작업은 글자 A, B 로 적는다. commit message, `docs/harness-roadmap.md`, 코드 주석이 작업을 번호로 가리키므로 작업 2 ~ 13 의 번호를 바꾸지 않는다.

1. base color 와 emissive texture 의 sRGB decode (끝남: 7b3ba43)
   - 입력 경로: PNG 를 WIC metadata 대로 읽어 sRGB slot 의 값을 decode 하지 않고 linear 로 썼다. spec 은 base color 와 emissive 를 sRGB 로 읽으라고 한다 (`Specification.adoc:2613, 2689`). image 의 colorspace metadata 는 무시해야 한다 (MUST, `:2359`).
   - 수정과 수치, 고치기 전과 뒤의 raster screenshot 쌍 (`Harness/kept/`) 은 7b3ba43 의 message 에 있다. 수치와 screenshot 은 옛 판의 것이다. Khronos 판에서 다시 돌린 확인 script 의 결과는 24b0057 의 message 에 있다.

- A. loader 의 multi-primitive packing 수정 (끝남: 0a3a9f5)
  - 결함: `Model/Loader/ModelConvert.cpp` 의 `CompileMesh` 는 같은 hash 의 primitive 를 한 Mesh 의 draw 여러 개로 묶을 때 vertex 개수와 index 개수를 byte offset 자리에 더했다. 옛 판은 Mesh 마다 draw 가 하나라 드러나지 않았고, Khronos 판은 triangle 의 81.3% 가 glTF 와 달랐다 (확인 script).
  - 수정과 수치는 0a3a9f5 의 message 에 있다.
- B. Khronos 판으로 교체 (끝남: 24b0057)
  - 수정과 수치, 작업 2 부터 쓰는 Validation: Depth Comparison 의 Log 줄 세 개는 24b0057 의 message 에 있다.

2. UV 를 float32 로 (끝남: e059154)
   - 입력 경로: UV0, UV1 을 `R16G16_FLOAT` 로 담아 Sponza 에서 base color texel 로 최대 14.1 texel 이 어긋났다. TEXCOORD 의 float accessor 값은 32 bit float 이므로 (`Specification.adoc:1143, 1603`) half 로 담으면 asset 과 다른 UV 로 그린다. unorm16 accessor 도 half 에 담으면 normalized 값의 변환에 32 bit 이상의 float 를 쓰라는 SHOULD (`:1154`) 를 지키지 못한다. PT 는 SHOULD 도 따르므로 고쳤다 (README "Rendering 기준").
   - 수정과 수치는 e059154 의 message 에 있다.
3. PT pass 와 누적 (끝남: dcc4615)
   - 입력 경로: alpha, doubleSided, metallic 은 spec 과 맞다. triangle 은 작업 A, base color 와 UV 는 작업 1, 2 가 고쳤다.
   - 수정과 수치, 확인하지 않은 것은 dcc4615 의 message 에 있다.
4. tangent 와 normal map 의 green 규약 확인
   - 입력 경로: glTF 는 normal texture 의 +Y 가 위이고 (`schema/material.schema.json:65`) UV (0, 0) 이 image 의 왼쪽 위이며 (`Specification.adoc:2498`), bitangent 는 `B = cross(N, T) · w` 다 (`:1763`). Khronos 판은 normal map 을 쓰는 primitive 모두에 TANGENT 가 있고 (전제), loader 는 그것을 읽어 `R10G10B10A2_UNORM` 으로 담는다 (`MeshConvert.cpp:285-288, 343-346`). engine 이 w 와 normal map 을 이 규약대로 읽는지 (`HitSurface.hlsli:70`, `DefaultPS.hlsl:228`) 는 아직 확인하지 않았다. TANGENT 가 없을 때 MikkTSpace 로 만드는 일은 뒤로 미뤘다 (뒤로 미룬 일).
   - `scripts/make-normal-map-test.py` 가 xy 평면에서 +z 를 보는 quad 를 쓴다. UV (0, 0) 은 왼쪽 위 corner 이고, normal map 의 모든 texel 은 glTF 규약으로 (0, 0.6, 0.8) 이다. license 문제가 없고 정답을 손으로 안다. TANGENT (1, 0, 0, 1) 인 판과, u 를 뒤집고 TANGENT 를 (−1, 0, 0, −1) 로 둔 판을 `-model` 로 열고 (OrbitCamera 로도 된다) Debug: Shading Normal 에서 world 의 +y 쪽 (색 (0.5, 0.8, 0.9)) 인지 본다. 반대면 (0.5, 0.2, 0.9) 이고, 그러면 engine 의 그 경로를 고친다.
   - 완료 기준: test quad 두 판이 모두 +y 다.
5. Lambert path 와 furnace 설정
   - 추가: `Model/PathTracer/LambertBrdf.hlsli` (ONB, cosine hemisphere, 교체 단위의 세 함수), `Model/PathTracer/SceneLights.hlsli` (단위 정의, sky 와 emission, 이 작업의 sky 는 상수). 수정: `PathTracerCS.hlsl`, panel.
   - Path 절을 sun 과 glTF BRDF 없이 만든다: emission, BRDF sampling 의 반사, Max Bounces, Russian roulette, NaN/Inf guard, Furnace, Geometric Normals. Material 은 Lambert 하나다.
   - 주석: Lambert 의 `glTF spec과 다르다`, RR 식에서 min 을 쓰는 이유, B 가 세는 반사, scene radiance 의 단위
   - 입력 경로: normal 과 tangent 의 규약은 작업 4 가 확인했다. normal texture 의 decode 와 `scale` 은 spec 과 맞다. leaf (thorn) 의 normal map 은 전제의 알려진 결함이다.
   - 완료 기준: 상수 sky 로 비친 Sponza 가 보이고, Max Bounces 를 1 에서 올리면 구석이 밝아진다. Furnace (albedo 1, emission 0) 와 Geometric Normals 에서 edge 와 texture 가 사라진 균일한 화면이 되고, Geometric Normals 를 끄면 normal 이 꺾인 곳만 어두워진다. Profiler 의 1 spp GPU time (B = 64, RR on, 기본 시점과 해상도) 을 적는다.
6. Export, 비교 script, furnace 의 수치
   - 추가: `Model/PathTracer/PathTracerCapture.h, .cpp`, `scripts/compare-images.py` (`diff`, `furnace`, `png`. DDS 는 `scripts/imagefiles.py` 로 읽는다). 수정: panel (Export, Export at Powers of 4, Log, invalid sample 수), `.gitignore`, `README.md` (준비에 Python 과 package).
   - Export at Powers of 4 를 켜면 sample 수가 1, 4, 16, … 에 이를 때마다 export 한다. Log 줄은 위 "재현" 의 형식이다.
   - 완료 기준 (furnace 는 모두 Lambert, Geometric Normals, RR off. 분산이 0 이라 16 spp 로 충분하다):
     - white furnace (albedo 1, emission 0, sky L, B = 1024): 모든 pixel 이 |X/L − 1| ≤ 1e-4 이고 invalid 가 0 이다. throughput 과 종료의 정규화를 본다.
     - emission furnace (albedo ρ = 0.5, emission Le = 0.5, sky `Le/(1 − ρ)` = 1, B = 1, 2, 4): 해는 어디서나 `L* = Le/(1 − ρ)` 다. path 하나의 값은 B 안에 escape 하면 L*, 아니면 `L*(1 − ρ^{B+1})` 다. 같은 Seed 와 B 의 white furnace X_WF 로 pixel 마다 `L*(1 − ρ^{B+1}(1 − X_WF/L))` 를 예측하고 모든 pixel 이 1e-4 안에서 맞는다. emission 의 누적, ρ < 1 의 반사, B 의 off-by-one 을 본다. Sponza 에서는 돌지 않는 emission 경로를 여기서만 본다. 두 run 이 같은 path 를 그리려면 RNG 를 쓰는 양이 albedo, emission, sky 에 따라 달라지지 않아야 한다.
     - Geometric Normals 를 끈 white furnace 의 `png` 와 평균. shading normal 과 README 의 n_g 규칙이 잃는 energy 의 지도다.
     - 상수 sky 에서 Seed 0 과 1 의 256 spp 를 `diff` 한 rRMSE 와 heatmap. heatmap 에 구조 없이 noise 만 보여야 한다. 같은 Seed 로 Reset 해 다시 누적한 두 export 의 rRMSE 는 0 이다.
     - furnace 는 self-hit, leak, visibility 오류와 방향 분포의 오류를 잡지 못한다. 무엇에 맞든 같은 값을 내기 때문이다.
7. Sun 과 raster 의 sun 단위
   - 추가: `Model/Raytracing/SurfaceRay.hlsli` (surface 에서 한 방향으로 나가는 ray, `DescribeSunRay` 를 옮김). 수정: `RaytracingDebugViewCS.hlsl`, `SceneLights.hlsli`, `PathTracerCS.hlsl` (NEE 와 다음 반사가 공용 함수를 쓴다), `PathTracer.cpp`, `ModelViewer.cpp`, `Model/Renderer/DefaultPS.hlsl`, `references/manifest.json`.
   - 입력 경로: raster 의 `ShadeDirectionalLight` 는 `c_light` 를 1/π 없이 곱한다 (`DefaultPS.hlsl:176-195`). sun 항 전체 (diffuse 와 specular) 를 π 로 나누고 `Sun Light Intensity` 의 기본값을 4 에서 4π 로 올려 raster 화면을 그대로 둔다. glTF 의 sparse 에 `/extensions/2.0/Khronos/KHR_lights_punctual/` 를 더해 lux 의 근거를 references 에 둔다. `ModelViewer.cpp:366` 의 ×125 (작업 B) 는 지금의 light 에 영향이 없어 남기고, `glTF spec과 다르다` 주석에 1 unit = 0.8 cm (계산) 와 거리로 감쇠하는 light 를 넣을 때 바꾼다는 것을 적는다.
   - 저장된 editor.ini 의 `Viewer/Lighting/Sun Light Intensity=4` 가 새 기본값을 덮어쓰므로 (`GameCore.cpp:96-107`) 12.5663706 으로 고친다. 고치지 않으면 raster 의 sun 이 π 배 어두워진다. commit message 에도 적는다.
   - 주석: Intensity 를 lux 로 읽는 이유 (KHR_lights_punctual 의 해당 줄)
   - 완료 기준: 기본 시점에서 고치기 전과 뒤의 raster screenshot 이 같다. sun 을 gizmo 나 Engine Tuning 으로 돌리면 PT 가 reset 되고 그림자가 따라온다. 그림자 경계가 Debug: Sun Visibility 와 같다.
8. glTF BRDF 와 BRDF Test
   - 추가: `Model/PathTracer/GltfBrdf.hlsli`, 생성 파일 `Model/PathTracer/GltfBrdfTables.hlsli`, `scripts/make-brdf-tables.py` (E_s 와 Ē_s 를 quadrature 로, 같은 lobe 식), `Model/PathTracer/BrdfTestCS.hlsl`. 수정: `PathTracerCS.hlsl` (Material 에 glTF 를 더하고 기본으로), `PathTracer.cpp`, panel, `scripts/compare-images.py` (`brdf`), `references/manifest.json` (Kulla & Conty 2017 의 course note 를 받을 수 있으면 더해 식을 대조한다).
   - BRDF Test 는 실제 HLSL 함수로 격자 (roughness 0.03, 0.1, 0.2, 0.3, 0.5, 0.7, 1.0 × metallic 0, 1 × μ_o 1, 0.7, 0.5, 0.3, 0.1, 0.03, baseColor 1) 마다 다음을 구해 CSV 와 Log 에 쓴다.
     - importance sampling 으로 낸 directional albedo 와 그 standard error σ (2²⁰ sample)
     - 반구를 촘촘한 격자로 적분한 albedo Q, 같은 격자의 ∫pdf, 무작위 방향 쌍의 `|f(l, v) − f(v, l)| / f`
   - 주석: coupling 과 table, α_min, lobe 선택, Capsaicin 에서 바꾼 D
   - 입력 경로: metallicRoughness 의 channel, factor, linear 는 spec 과 맞다.
   - 완료 기준:
     - roughness ≥ 0.1 의 모든 칸에서 importance sampling 과 Q 가 `4σ + 1e-3` 안에서 맞는다. sampling 과 pdf 의 불일치를 잡는다 (roughness 0.03 은 lobe 가 격자보다 좁아 importance sampling 결과만 본다).
     - metallic 0 의 albedo 가 1 에서 2e-3 안이고 (table 보간의 오차, 처음 측정으로 고친다), 모든 칸의 albedo 가 1 + 1e-3 이하다. dielectric 이 1 이면 HLSL 과 table script 의 식이 서로 맞다는 뜻도 된다.
     - 상호성 오차 ≤ 1e-4, ∫pdf ≤ 1 + 1e-3
     - glTF white furnace (baseColor 1, metallic 0, Geometric Normals, RR off) 의 image 평균이 1 에서 2e-3 안이다. specular 와 lobe 선택까지 지나는 장면 단위 test 다. 분산이 0 이 아니므로 평균으로 본다.
     - 기본 시점에서 Sky 를 끈 B = 1 의 PT 와 Environment 가 None 인 raster 의 screenshot 쌍 (같은 Exposure). `brdf` 그래프 (roughness 별 albedo).
9. Sky
   - 수정: `PathTracer.cpp` (radiance cube 의 `TextureCube` SRV slot, static linear sampler), `SceneLights.hlsli`, `ModelViewer.cpp` (`g_IBLTextures` 에서 고른 radiance cube 를 넘긴다).
   - raster 의 `SetIBLTextures` 는 쓰는 중일 수 있는 descriptor 를 바로 덮어쓴다 (`Renderer.cpp:284-308`). PT 는 `SceneHeapTable.h:9-11` 을 지키려고 cubemap 이 바뀐 frame 에만 GPU 를 기다린 뒤 `PointTo` 한다.
   - 주석: mip 0 과 cd/m² 로 읽는 이유
   - 완료 기준: primary ray 가 miss 한 곳의 sky 가 raster skybox 와 같은 방향이고 더 선명하다. Environment 를 바꾸면 reset 되고 조명이 바뀐다. 기본 시점에서 raster 와 PT 의 screenshot 쌍을 남긴다 (Adaptation off, 같은 Exposure, PT 는 Target 까지). Atrium 의 천창과 조명이 firefly 를 만드는지 1024 spp 의 heatmap 으로 보고 적는다.
10. Log 와 Restore
    - 추가: `Core/Editor/SceneConditions.h, .cpp`. 수정: `RayQueryValidation.h, .cpp`, `ModelViewer.cpp`, `Core/Core.vcxproj`, PT panel 과 capture.
    - 완료 기준: export 옆 `.txt` 의 줄을 app 을 다시 실행해 Restore 하고 같은 sample 수로 다시 export 하면 `diff` 의 rRMSE 가 1e-6 이하다. 검증 panel 에서 예전 Log 줄도 Restore 된다.
11. Indirect Irradiance
    - 수정: `PathTracerCS.hlsl`, panel, capture.
    - 주석: E 의 정의, x₁ 의 emission 과 BRDF 를 빼는 이유, DDGI 경로가 Lambert 를 쓰는 이유
    - 완료 기준: 화면에서 sun 의 그림자 경계와 x₁ 의 texture 가 사라진다. white furnace 에서 모든 pixel 이 |E/(πL) − 1| ≤ 1e-4 이고, emission furnace 에서 지수가 B 인 항등식 `πL*(1 − ρ^B (1 − E_WF/(πL)))` 이 1e-4 안에서 맞는다.
12. 수렴과 측정
    - 수정: `scripts/compare-images.py` (`agree`, `convergence`), `PathTracerPanel.cpp` (Target Samples 기본값).
    - 사용자가 고정 시점 3곳 (예: 햇빛이 드는 바닥, 그림자 속 회랑, curtain 이 있는 2층) 을 고르고 Log 줄을 남긴다. 아래 측정은 첫 시점에서 한다. 나머지 두 곳은 ground truth 의 spp 로 Seed 0, 1 만 누적해 export 한다. 이 GPU 시간은 작업 13 (a) 의 script 를 쓰는 동안 돌린다.
    - 완료 기준 (Indirect Irradiance, Material glTF):
      - 16 ~ 4096 spp 에서 rRMSE(Seed 0, Seed 1) 의 log-log 기울기가 −0.5 ± 0.05 다.
      - rRMSE(Seed 0, Seed 1) ≤ 1% 가 되는 가장 작은 4의 거듭제곱을 ground truth 의 spp 로 정해 Target Samples 기본값으로 둔다. 그때 한 run 의 오차는 약 0.7% (= 1%/√2) 다. 10분 안에 이르지 못하면 잰 곡선으로 기준을 다시 정해 적는다. flagpole 같은 roughness 0 근처 texel 에 비친 sun 이 firefly 로 수렴을 막으면 그 영역을 적는다.
      - 1024 spp 에서 `agree` 의 r 이 RR on 대 off, B = 64 대 1024 에서 각각 1.1 이하다. r = 1.1 이면 bias 가 noise 의 0.46 배까지다. r 은 수백만 pixel 의 합이라 bias 가 없으면 1 근처에 모인다. firefly 가 지배하면 흔들리므로 그때는 heatmap 과 같이 본다.
      - DDGI 경로 (Material Lambert) 와 전체의 rRMSE. DDGI 가 계산하지 않는 path 의 몫이다.
      - Radiance 의 B = 1, 2, 4, 64 image 와 B = 64 대비 rRMSE. Indirect Irradiance 의 B = 2 대비 B = 64 (DDGI single bounce 가 빠뜨리는 빛)
      - invalid sample 0. Profile build 에서 1 spp 의 GPU time, sample/s, ground truth 까지 걸린 시간
13. Cornell box 와 외부 renderer
    - (a) 장면과 load
      - 추가: `scripts/make-cornell-box.py` (Bitterli 의 CC0 Cornell Box (`https://benedikt-bitterli.me/resources/mitsuba/cornell-box.zip`) 수치로 glTF 와 Mitsuba XML, PLY 를 같은 triangle 로 쓴다. `metallicFactor: 0`, mesh 하나에 primitive 하나, light 는 아래로 방출하는 single-sided quad 와 위를 보는 검은 quad, UV0 있음). 출력은 `.gitignore` 대상인 `MiniEngine/ModelViewer/CornellBox/` 다.
      - 입력 경로: loader 가 `KHR_materials_emissive_strength` 를 읽고 `.mini` version 을 올린다. light 의 radiance (17, 12, 4) 가 core 의 emissiveFactor 범위 [0, 1] 을 넘는다. `-model` 로 연 장면도 EditorCamera, Log, Restore 를 쓰고 scale 을 인자로 고정한다 (`ModelViewer.cpp:358-394`).
      - 완료 기준: Cornell box 가 PT (Material Lambert) 에 보이고 Log 줄로 camera 가 되돌아온다. 그 zip 에 같이 온 Tungsten reference image (`TungstenRender.exr`) 와 눈으로 견준다. FOV 가 세로 45° 로 고정이라 구도는 다르다.
    - (b) reference render 와 비교
      - reference 는 Mitsuba 3 (`pip install mitsuba`) 이나 설치된 Blender 5.2 로 그린다. box filter, float32, 세로 45° FOV, `max_depth = B + 1`, PT 격자의 반 pixel 어긋남 (`CameraRays.cpp:18-20`) 을 맞춘다. 반 pixel 은 2 배 해상도로 그려 한 칸 옮긴 2×2 평균으로 맞추면 renderer 의 규약에 영향을 받지 않는다.
      - PT 는 Material Lambert 다 (Bitterli 는 순수 diffuse 다). emissive NEE 가 없어 light 에 맞을 확률이 sample 당 약 1.45% 이므로 16 × 16 tile 평균으로 비교한다.
      - 완료 기준: tile 평균의 `agree` r (PT 와 reference 를 각각 두 Seed 로) 이 1.1 이하다. leak, self-hit, visibility, 방향 분포, emissive 의 data 경로처럼 furnace 가 못 잡는 오류를 여기서 본다.
      - (b) 가 PT 의 bug 를 찾아도 10/29 에는 고치지 않는다. 고치기와 작업 12 의 재측정 (export 가 script 로 되어 있어 GPU 시간이 대부분이다) 은 발표 뒤에 하고, 발표의 측정에는 고치기 전의 값이라고 적는다.
    - time box: (a) 와 (b) 는 끝나지 않아도 기한이 되면 멈춘다. (b) 가 통과하면 그 image 와 r 을 발표 자료에 넣고, 통과하지 못하면 같은 시점의 PT 와 reference image, 그때의 r, 원인 후보를 "진행 중" 으로 보인다. (b) 는 처음에 10/28 이었지만 앞의 작업이 늘어 10/29 로 옮겼다. DDGI 와의 비교 수치는 (b) 가 통과한 뒤에 믿는다.

## 일정

작업마다 끝낼 기한만 정한다. 작업에 드는 시간은 해 봐야 알고 같은 작업도 기한에 따라 들이는 시간이 달라지므로, 작업 시간은 추정하거나 기록하지 않는다. 날짜만 적은 기한은 그날이 끝날 때다. 작업은 기한 안에 끝내고 임의로 뒤로 미루지 않는다.

| 기한 | 끝낼 작업 |
|---|---|
| 10/24 | 끝남: 1, A, B, harness 의 D3D12 log 수집 (573dded) 과 그 log 로 찾은 Lighting 수정 (e8a2352), 문서 정리 |
| 10/25 | 끝남: 2, 3. 남은 것: 4 |
| 10/26 | 5, 6, 7 |
| 10/27 | 8, 9, 10 |
| 10/28 | 11, 12, 13 (a) |
| 10/29 오전 | 13 (b) |
| 10/29 | 발표 자료 |

- 10/29 는 13 (b) 의 time box 와 발표 자료의 날이라 밀린 작업을 넘기지 않는다.
- 순서의 이유
  - 작업 1, 2, 4 (입력 경로) 를 PT 의 shading 앞에 둔다. PT 가 정답이므로 그 입력이 먼저 spec 을 따라야 하고, 늦게 고치면 그 앞의 screenshot, furnace, 측정을 다시 해야 한다. 작업 3 (PT pass) 은 normal 을 쓰지 않으므로 작업 4 앞에 두어 PT 의 비용과 문제를 일찍 본다.
  - 작업 A, B 는 작업 2 앞이다. B 가 옛 판을 지우므로, 옛 판의 수치 (eb009f6) 로 확인할 수 있는 A 를 먼저 한다. PT 의 입력이 처음부터 Khronos 판이어야 그 뒤의 확인과 screenshot 을 다시 하지 않으므로 B 를 작업 2, 3 앞에 둔다.
  - 작업 10 (Log 와 Restore) 은 작업 11, 12 앞이다. 측정의 고정 시점과 작업 13 의 camera 가 Restore 를 쓰고, 발표가 restart 뒤의 재현을 보일 수 있다.
  - 작업 13 은 마지막이다. 독립된 estimator 와 맞춰 보는 가장 강한 근거지만 원인 찾기의 상한을 알 수 없다. 작업 12 뒤에 두어 발표의 핵심 자료를 먼저 확보하고, (a) 의 script 를 쓰는 동안 작업 12 의 GPU run 을 돌린다. (a) 와 (b) 는 기한에서 멈춘다 (작업 13 의 time box).
- 10/24 ~ 10/28 이 밀리면 이 순서로 발표 뒤로 옮긴다.
  1. 작업 13 (a): 10/29 의 (b) 도 할 수 없으므로 외부 renderer 비교 전체가 계획이 된다.
  2. 작업 8 의 `brdf` 그래프와 glTF white furnace (작업 12 뒤로): 발표의 BRDF 근거가 BRDF Test 의 표 하나가 된다.
  3. 작업 12 의 두 번째, 세 번째 시점과 bounce 측정 (DDGI 단계로)
  4. 작업 10: 측정 시점을 검증 panel 의 Restore 와 손으로 되돌린다. 발표는 재현성을 같은 실행 안에서만 보인다.
  5. 작업 11: 작업 12 를 Radiance 로 한다. 발표는 DDGI 와 비교할 양을 정의로만 보인다.
  - 작업 A, B, 2, 4 는 빼지 않는다. PT 의 입력이 glTF 와 같고 spec 을 따른다는 것이 정답의 전제다.

## 발표 자료

10/29 에는 오전의 작업 13 (b) 말고 새로 재지 않도록 아래를 각 작업의 완료 기준이 남긴다.

| 보여 줄 것 | 작업 |
|---|---|
| coupling 별 directional albedo (`fresnel_mix` 1.27, `(1 − F)(1 − F)` 의 grazing, Kulla & Conty 1.0), 단위 (lux, cd/m²) | 이 문서 |
| asset 을 Khronos 판으로 바꾼 이유 (validator, 옛 판과 같은 geometry) 와 알려진 결함, packing 을 고치기 전과 뒤의 틀린 triangle 비율 | 이 문서, A, B |
| PT 의 입력을 spec 에 맞춘 것: sRGB decode 전과 뒤의 raster (옛 판), float16 UV 의 최대 14.1 texel, test quad 두 판 | 1, 2, 4 |
| white furnace, emission furnace (B 항등식), shading normal 이 잃는 energy, 같은 Seed 의 재현 | 6 |
| sun 단위를 바꿔도 raster 화면이 그대로다 | 7 |
| BRDF Test 의 표와 roughness 별 albedo, glTF white furnace, sun 직접광 (B = 1) 과 raster sun | 8 |
| 같은 시점의 raster 와 PT | 9 |
| app 을 다시 실행해도 같은 Log 줄이 같은 image 를 낸다 (rRMSE) | 10 |
| DDGI 가 비교할 Indirect Irradiance 와 DDGI 경로의 몫 | 11, 12 |
| spp 별 image (1, 16, 256, 4096), rRMSE–spp 그래프와 기울기, RR 과 B 의 r | 12 |
| GPU time, sample/s, ground truth 까지의 시간 | 5, 12 |
| Cornell box 의 PT 와 외부 renderer image, tile 평균 r. 기한 안에 통과하지 못하면 "진행 중" 과 원인 후보 | 13 |

## 범위 밖

- raster 의 sun 항 밖의 shading: Burley, `c_diff` 의 0.96, Hable visibility, specular 와 diffuse IBL, SSAO. PT 가 쓰지 않는 코드라 README "Rendering 기준" 의 규칙이 이 작업에 요구하지 않는다. raster 와 수치로 비교하는 검증도 하지 않는다 (8432ed7).
- BLEND material, skinned mesh, animation 과 instance 변경의 감지, 여러 ModelInstance
- glTF 의 KHR_lights_punctual light 를 load 하는 일, point 와 spot light, sun 의 angular size
- DoF 같은 lens model, denoiser, texture LOD (ray cone 등)
- Sponza 가 쓰지 않는 loader 기능. 그 기능을 처음 쓰는 asset 의 작업이 다룬다.
- asset 의 값을 고치는 일. 전제의 알려진 결함도 공개된 그대로 쓴다.

## 뒤로 미룬 일

- TANGENT 가 없을 때 MikkTSpace 로 tangent 를 만드는 일 (`Specification.adoc:1759`): TANGENT 없이 normal map 을 쓰는 asset 을 처음 쓸 때. 그때까지는 지금처럼 DirectXMesh `ComputeTangentFrame` 이 만든다 (`MeshConvert.cpp:290-327`). 계획은 7b3ba43 의 이 문서 작업 4 (a) 에 있다.
- metal 의 multiple scattering (Kulla & Conty 의 energy compensation 항): rough 한 metal 의 energy 손실이 결과를 바꾸는 장면이나 RT Reflection 전. 그러면 metal 의 furnace 도 1 이 된다.
- sky 의 importance sampling 과 MIS: 작업 9, 12 에서 Atrium 의 천창이나 조명이 firefly 를 만들거나 1% 에 이르지 못할 때
- emissive triangle 의 NEE: Cornell box 처럼 작은 emitter 가 수렴을 막을 때
- Sobol 같은 stratified sampler, blue noise: 수렴 시간이 문제가 될 때
- infra 검증 5-4 ~ 5-7 의 mode: PT 에 그 문제로 보이는 결과가 나올 때 (323e185)
- raster 의 irradiance cube 와 PT 의 가리지 않은 sky irradiance 를 app 안에서 비교하는 mode
- DDGI 가 light 와 Lambert 를 쓰기 시작하면 `SceneLights.hlsli`, `LambertBrdf.hlsli` 를 여러 기능이 공유하는 위치로 옮긴다 (README "폴더 구조")
