# Reference Path Tracer 로드맵

DDGI 와 RT Reflection 의 정답 image 를 만드는 path tracer (PT) 를 inline RayQuery compute pass 로 만든다. 작업 branch 는 `feat/reference-path-tracer` 이고, 아래 작업을 끝내면 `main` 에 merge 한다. 10/30 WEEK 3 에 발표한다.

이 문서는 기한 안에 할 작업과 그 순서만 정한다. 작업의 설계와 완료 기준은 그 작업을 시작할 때 코드와 spec 을 보고 정한다. 미리 정한 세부는 작업을 시작하면 달라지고, 그때마다 이 문서를 코드와 맞추는 비용이 들기 때문이다. 끝난 작업의 확인은 그 작업이 남긴 script 가, 다시 재면 바뀌거나 다시 재기 비싼 수치는 [측정 기록](reference-path-tracer-measurements.md) 이, 코드보다 오래 남아야 할 이유는 코드 주석이 갖는다. 끝난 작업이 뒤로 넘긴 일은 그 일을 할 작업이나 "뒤로 미룬 일" 에 적고, 끝난 작업에는 commit 만 적는다.

def659b 의 이 문서에는 미리 조사해 둔 설계 (출력, BRDF, light, sampling, 누적, export 와 비교) 와 작업별 완료 기준이 있다. 작업을 시작할 때 출발점으로 참고하되 그대로 따르지 않는다.

## 목표

- glTF metallic-roughness material 을 bias 없이 path tracing 한 정답을 만든다. PT 와 PT 가 쓰는 engine 코드는 README "Rendering 기준" 을 따른다.
- 정답이 맞다는 것을 furnace, BRDF 단위의 test, 수렴 측정, 외부 renderer 와의 비교로 보인다. DDGI 와 PT 의 비교 수치는 외부 renderer 와 맞춘 작업 13 이 통과한 뒤에 믿는다.
- DDGI 가 같은 시점에서 비교할 양을 정의한다. DDGI 의 오차를 irradiance 근사의 오차와 probe hit 의 shading 이 정답과 다른 몫으로 나눌 수 있어야 한다 (PathTracer.h 의 Output).
- 측정 조건을 Log 한 줄로 되돌리고 결과를 float image 로 export 해서, DDGI 가 같은 조건에서 비교하게 한다.

## 전제

- 장면은 EditorCamera 로 연 정적 Sponza (KhronosGroup glTF-Sample-Assets 판, 24b0057) 다.
- GPU 는 RTX 4060 Laptop 이다.

## 작업

commit message 와 `docs/harness-roadmap.md` 가 작업을 번호로 가리키므로 번호를 바꾸지 않는다. 번호를 정한 뒤에 더한 작업은 글자로 적는다. A, B 는 작업 1 뒤에, C 는 작업 6 뒤에 더했다.

1. base color 와 emissive texture 의 sRGB decode (끝남: 7b3ba43)

- A. loader 의 multi-primitive packing 수정 (끝남: 0a3a9f5)
- B. Sponza 를 Khronos 판으로 교체 (끝남: 24b0057)

2. UV 를 float32 로 (끝남: e059154)
3. PT pass 와 누적 (끝남: dcc4615)
4. tangent 와 normal map 규약 (끝남: f931539)
5. Lambert path 와 furnace (끝남: cf52623, a5060e3)
6. Export 와 비교 script (끝남: 4cdb55e, 2b33c11)

- C. Raster 의 IBL 과 Environment 제거 (끝남: 64b37fb, a5dd835, 35e8e58)

7. Directional Light (끝남: f57e319, c24314b, 648aea9, 7d8148f, c8610b9, 665bea1)
8. glTF BRDF (끝남: 5ac463b, d9a4e3f, 77271b2)
9. Sky Light (끝남: 63404ca, a99f3fe, 319d723, f5b807e)
10. Log 와 Restore (끝남: 773ada1, 39d9b61)
11. Indirect Irradiance (끝남: a93d5bb)
12. 수렴과 측정 (끝남: d1ad157, 60be7ec)
13. Cornell box 와 외부 renderer: (a) 장면을 만들어 load 하고, (b) 외부 renderer 로 그린 image 와 비교한다. furnace 가 잡지 못하는 leak, self-hit, visibility, 방향 분포의 오류를 여기서 본다. `-model` 로 연 장면은 orbit camera 라서 측정 조건의 줄로 Restore 할 수 없으므로 (39d9b61), (a) 가 그 장면에서도 camera 를 되돌리게 한다.

## 일정

작업마다 끝낼 기한만 정한다. 작업에 드는 시간은 해 봐야 알고 같은 작업도 기한에 따라 들이는 시간이 달라지므로, 작업 시간은 추정하거나 기록하지 않는다. 날짜만 적은 기한은 그날이 끝날 때다. 작업은 기한 안에 끝내고 임의로 뒤로 미루지 않는다.

| 기한 | 끝낼 작업 |
|---|---|
| 10/24 | 끝남: 1, A, B, harness 의 D3D12 log 수집 (573dded) 과 그 log 로 찾은 Lighting 수정 (e8a2352), 문서 정리 |
| 10/25 | 끝남: 2, 3, 4 |
| 10/26 | 끝남: 5, 6, C, 7 |
| 10/27 | 끝남: 8, 9, 10 |
| 10/28 | 끝남: 11, 12. 남은 것: 13 (a) |
| 10/29 오전 | 13 (b) |
| 10/29 | 발표 자료 |

- 10/29 는 13 (b) 와 발표 자료의 날이라 밀린 작업을 넘기지 않는다.
- 작업 13 은 끝나지 않아도 기한이 되면 멈춘다. (b) 가 통과하지 못하면 그때의 결과와 원인 후보를 "진행 중" 으로 발표한다. (b) 가 PT 의 bug 를 찾아도 고치기와 작업 12 의 재측정은 발표 뒤에 한다.

### 순서의 이유

- 작업 4 는 normal 을 쓰는 shading (작업 5 부터) 앞이다. PT 가 정답이므로 그 입력이 먼저 spec 을 따라야 하고, 늦게 고치면 그 뒤의 확인과 측정을 다시 해야 한다. 작업 1, A, B, 2 도 같은 이유로 앞에 두었다.
- 작업 5, 6 은 답을 식으로 아는 furnace 로 path loop 를 먼저 확인한다. 작업 7, 8 은 그 loop 에 directional light 와 glTF BRDF 를 하나씩 더한다.
- 작업 C 는 작업 7 앞이다. raster 의 빛을 directional light 와 emission 으로 먼저 줄여서, 작업 7 이 raster 에서 바꾸는 빛을 directional light 하나로 둔다.
- 작업 9 는 작업 7 뒤다. 상수 sky 의 값은 directional light 의 lux 와의 비로 정하므로 그 단위가 먼저 있어야 한다.
- 작업 10 은 작업 11, 12 앞이다. 측정의 고정 시점과 작업 13 의 camera 가 Restore 를 쓰고, 발표가 restart 뒤의 재현을 보일 수 있다.
- 작업 13 은 마지막이다. 독립된 estimator 와 맞춰 보는 가장 강한 근거지만 원인 찾기의 상한을 알 수 없으므로, 작업 12 뒤에 두어 발표의 핵심 자료를 먼저 확보한다. (a) 를 하는 동안 작업 12 의 GPU run 을 돌린다.

### 밀릴 때

10/28 까지의 작업이 밀리면 이 순서로 발표 뒤로 옮긴다.

1. 작업 13 (a). 10/29 의 (b) 도 할 수 없으므로 외부 renderer 와의 비교 전체가 계획이 된다.
2. 작업 8, 12 에서 발표의 핵심이 아닌 측정
3. 작업 10. 측정 시점을 손으로 되돌리고, 발표는 재현을 같은 실행 안에서만 보인다.
4. 작업 11. 작업 12 를 화면의 radiance 로 재고, 발표는 DDGI 와 비교할 양을 정의로만 보인다.

## 발표

10/29 에는 작업 13 (b) 말고 새로 재지 않는다. 각 작업은 끝날 때 발표에 쓸 image 를 남기고, 수치는 측정 기록에 남긴다.

- PT 의 입력을 spec 에 맞춘 것: 1, A, B, 2, 4
- PT 가 맞다는 근거: furnace (5, 6), BRDF 단위의 test (8), 수렴과 bias (12), 외부 renderer 와의 비교 (13)
- raster 에서 IBL 과 Environment 를 빼고 sky 를 상수로 둔 이유, directional light 의 단위와 BRDF 를 고른 이유: C, 7, 8, 9
- 같은 시점의 raster 와 PT, restart 뒤의 재현: 9, 10
- DDGI 가 비교할 양과, 그 양으로 DDGI 의 오차를 나누는 Shading DDGI Probe Hit: 11, d84b6c3
- 비용 (GPU time, ground truth 까지의 시간): 5, 12

## 범위 밖

- raster 에서 directional light 항 밖의 shader 코드 (normal map, emission, alpha mask 의 적용) 를 spec 에 맞추는 일
- raster 의 visibility 를 PT 와 같게 만드는 일. raster 는 directional light 의 visibility 를 shadow map 으로 근사한다.
- Sponza 같은 장면에서 raster 와 PT 를 수치로 비교하는 검증. 둘의 차이에 shadow map 의 근사와 위의 shader 코드가 섞인다. occluder 가 없는 test scene 에서는 작업 8 이 비교한다 (`scripts/check-gltf-brdf.py` 의 [R1]).
- BLEND material, skinned mesh, animation 과 instance 변경, 여러 ModelInstance
- glTF 의 KHR_lights_punctual light 를 load 하는 일, point 와 spot light, directional light 의 angular size
- lens model, denoiser, texture LOD
- Sponza 가 쓰지 않는 loader 기능. 그 기능을 처음 쓰는 asset 의 작업이 다룬다.
- asset 의 값을 고치는 일. 알려진 결함이 있어도 공개된 그대로 쓴다.

## 뒤로 미룬 일

- TANGENT 가 없을 때 MikkTSpace 로 tangent 를 만드는 일 (`Specification.adoc:1759`): TANGENT 없이 normal map 을 쓰는 asset 을 처음 쓸 때. 계획은 7b3ba43 의 이 문서 작업 4 (a) 에 있다.
- metal 의 multiple scattering (`Specification.adoc:3637` 이 권한다): rough 한 metal 의 energy 손실이 결과를 바꾸는 장면이나 RT Reflection 전. 지금은 single scattering 이라 baseColor 1, roughness 1 인 metal 이 수직에서 0.31 만 반사한다 (작업 8 의 `kept/gltf-brdf/brdf/cells.json`).
- glTF BRDF 의 coupling table 을 grazing 에서 더 촘촘히 두는 일: E_s 의 보간 오차가 μ ≥ 0.03 에서 3.0e-4, 그 아래에서 7.3e-4 까지이고 energy 가 느는 쪽은 3.4e-4 까지다 (`scripts/gltfbrdf.py`). 작업 12 의 bias 가 grazing 에 몰릴 때. 작업 12 는 bias 를 Lambert 로만 재서 이 조건을 보지 못했다.
- 방향에 따라 다른 sky 와 그 importance sampling, MIS: 상수 sky 로는 볼 수 없는 결과가 필요해질 때. 그때 맞는 sky 를 구하거나 만든다.
- 상수 Sky Light 의 NEE 와 MIS: 작업 12 가 sky 에서 오는 빛 때문에 수렴이 느리다고 잴 때. 작업 12 는 sky 에서 오는 빛의 몫을 따로 재지 않았다. 상수 sky 의 방향을 cosine 으로 고르면 pdf 가 Lambert 의 BRDF sampling 과 같아서 얻는 것이 적다. Sponza 의 실내에서는 sky 가 courtyard 로만 보이므로, 그 구멍 쪽을 고르는 portal sampling 이 있어야 효과를 볼 것이다 (추측).
- emissive triangle 의 NEE: 작은 emitter 가 수렴을 막을 때
- stratified sampler, blue noise: 수렴 시간이 문제가 될 때
- 정답의 spp (Target Samples 기본값) 를 131072 에서 262144 로 올리는 일: 작업 13 이나 DDGI 비교에서 정답의 noise 가 결과를 바꿀 때. corridor 시점의 Lambert 는 131072 spp 에서 e 가 1% 에 가까스로 들어, 1% 와의 차가 e 의 표준오차와 비슷하다 (`scripts/check-convergence.py check` 의 [C2]).
- glTF 의 caustic 이 남기는 흰 점을 줄이는 일: 작업 12 의 수렴 측정에서 흰 점이 수렴 목표를 막았으므로, 작업 12, 13 과 따로 줄인다. 흰 점 하나는 sample 하나가 만든다. 흰 점을 만든 sample 의 path 에서는 햇빛이 glossy 한 면에서 반사되어 다른 면을 비춘다. 햇빛을 반사한 면은 대부분 Sponza 의 천 (material 17, 18, 19) 에 있는 금색 부분이다. 금색 부분의 roughness 는 0.18 이고 metallic 은 0.996 이다. roughness 가 0.05 보다 작은 면에서 햇빛이 반사된 흰 점은 Indirect Irradiance 에서 2%, Radiance 에서 5% 다. 아래 값의 복도 시점은 pose `-1479.30859 211.686859 395.723236 -1.53974497 -0.124806166`, light direction `0,0.970682025,-0.24036777`, intensity 4 이고, 조건은 Max Bounces 64, Russian roulette on, 4096 spp 다.
  - 작업 8 을 끝낼 때 Radiance 로 잰 흰 점 (8 이웃의 최대보다 0.004 넘게 밝고 2 배 이상인 pixel) 은 glTF 344 개, Lambert 27 개다. Seed 를 바꾸면 같은 자리는 42 개이고 평균은 같다. 흰 점은 sky 만 켜면 거의 사라지고, Max Bounces 1, 2, 3 (1024 spp) 에서 42, 108, 324 개다. 흰 점 하나를 만든 sample 의 값은 중앙값 27, 최대 8281 이다. Lambert 의 sample 은 약 intensity / π 를 넘지 못한다.
  - 작업 12 의 Indirect Irradiance 에서 Lambert 는 두 시점 모두 131072 spp 에 noise 1% 안에 들었다. glTF 는 16384 spp 까지 쌓아도 1% 에 멀고, start 시점은 8192 spp 보다 16384 spp 의 noise 가 컸다. 두 시점 모두 오차의 대부분이 상위 0.1% 의 pixel 에 있다. 수치는 측정 기록의 작업 12 에 있다.
  - 원인은 작업 12 뒤에 복도 시점과 Seed 0 으로 조사했다. PathTracerCS.hlsl 에 임시 계측을 넣어, pixel 마다 가장 큰 sample 에서 가장 큰 항 (햇빛의 NEE, sky, emission) 의 종류, vertex, 앞 vertex 를 기록했다. 계측을 넣은 build 의 평균은 `kept/convergence/slope/corridor-gltf-seed0-4096.dds` 와 bit 단위로 같았다. 계측은 repo 에 남기지 않았다. 아래 값은 Indirect Irradiance 의 값이고, 괄호 안은 Radiance 의 값이다. Indirect Irradiance 의 흰 점은 E / π 로 판정했다.
  - 흰 점은 1157 개 (344 개) 다. 흰 점의 92% (86%) 에서는 가장 큰 sample 을 N 으로 나눈 값이, pixel 이 이웃의 중앙값보다 밝은 양의 절반을 넘는다. 두 값의 비는 중앙값이 0.994 (0.998) 다. 흰 점은 sample 하나가 만들고, 다른 큰 sample 이 없으면 1/N 로만 줄어든다.
  - 흰 점 중 1105 개 (327 개) 는 햇빛의 NEE 가 만들었다. 햇빛을 반사한 vertex 는 천의 금색 부분이 887 개 (256 개), 식물 잎 (material 0, 3) 이 105 개 (43 개), material 13 의 metal 이 68 개 (8 개) 다. 햇빛을 반사한 vertex 의 roughness 는 0.1 ~ 0.2 가 766 개 (229 개) 이고, 0.05 보다 작은 경우는 20 개 (16 개) 다. 계측이 읽은 roughness 는 asset 의 metallicRoughness texture 를 mip 0 에서 읽은 값과 같았다.
  - 앞 vertex 는 대부분 돌 바닥 (material 9, roughness 약 0.9) 이고, 흰 점 중 879 개 (278 개) 에서 diffuse lobe 로 방향을 골랐다. sample 값은 throughput × f cos × illuminance 와 맞고 (비 1.008), throughput 은 두 output 모두 1.01 이하다. 따라서 앞 반사들의 f cos / pdf 가 sample 을 키우지는 않았다. Russian roulette 의 1 / survival 은 반사 3 번째부터인 292 개 (110 개) 에서 sample 을 4 ~ 1289 배 (17 ~ 1666 배) 키웠다.
  - 대조 실험의 흰 점은 Lambert 가 82 개 (27 개) 다. 모든 vertex 의 roughness 에 하한 0.05 를 두면 1173 개 (355 개), 하한 0.3 을 두면 670 개 (124 개), 천에만 하한 0.3 을 두면 739 개 (156 개) 다. camera ray 가 맞은 vertex 를 제외하고, 나머지 vertex 의 햇빛 NEE 에서 specular 항을 빼면 83 개 (47 개) 다. 천에서만 빼면 278 개 (91 개) 다. 즉 specular 항을 빼면 Lambert 와 비슷해지고, 천에서만 빼도 흰 점의 76% (74%) 가 사라진다.
  - start 시점 (`scripts/check-convergence.py` 의 POSES) 의 흰 점은 369 개 (174 개) 이고, 56% (44%) 에서 천의 금색 부분이 햇빛을 반사했다. start 시점의 Indirect Irradiance 에서 Lambert 의 흰 점은 106 개다.
  - sample 하나가 큰 이유 (식에서 한 추론): 햇빛은 delta light 라서 금색 면에서는 NEE 로만 반사된다. 금색 면이 햇빛을 반사하는 f cos 의 peak 는 F / (4π α² μo) 에 가깝고 (μo = n·v), roughness 0.18 의 α 0.032 에서는 roughness 1 의 약 1000 배다. 바닥의 diffuse vertex 가 금색 면의 반사를 찾으려면, cosine sampling 이 금색 면에서 햇빛이 거울 반사되는 방향 근처를 골라야 한다. 고를 수 있는 방향의 solid angle 은 α² 에 비례한다. 따라서 금색 면이 평균에 더하는 몫은 α 와 거의 무관하지만, sample 의 분산은 1/α² 로 커진다. 배경이 어두운 복도에서는 sample 과 이웃의 비가 특히 크다.
  - 줄이는 방법: sample 값을 자르는 일은 bias 가 생겨 정답에 맞지 않는다. roughness 하한을 올리면 glTF 의 정답이 아니게 되고, 0.3 까지 올려도 흰 점이 42% (64%) 만 줄었다. directional light 의 angular size 와 MIS (범위 밖에서 옮겨야 한다) 도 효과가 적을 것이다 (추론). 태양의 반지름 약 0.27° (0.0047 rad) 는 금색 면의 반사 lobe 폭 (약 2α = 0.065 rad) 보다 훨씬 작아서, 햇빛이 반사되는 방향의 범위를 거의 넓히지 못한다. MIS 는 금색 vertex 에서 방향을 고르는 일만 바꾸고, 바닥 vertex 가 금색 면 쪽을 고를 확률은 바꾸지 않는다. 남은 후보는 빛 쪽에서 path 를 만드는 방법 (light tracing, bidirectional path tracing) 과 path guiding 이다 (추측).
  - DDGI 비교에 주는 영향: 복도 시점의 Indirect Irradiance 에서 image 평균 (E / π) 은 glTF 0.008751, Lambert 0.007570 이다. 위 대조 실험에서 specular 항을 빼면 glTF 의 평균이 0.007914 가 되어, 두 평균의 차의 71% 가 사라진다. Radiance 에서는 35% 가 사라진다. 따라서 glTF 가 Lambert 보다 밝은 몫의 대부분은 햇빛이 specular 로 반사되어 다른 면을 비추는 path 의 몫이고, 흰 점도 같은 path 에서 나온다. DDGI 의 probe hit shading 이 정답과 다른 몫은 이 차가 아니라 glTF 와 Shading DDGI Probe Hit 의 차이고 (PathTracer.h 의 Output), 재지 않았다. image 평균은 Seed 0, 1 과 4096, 16384 spp 사이에서 0.03% 안으로 같지만, pixel 마다의 값은 수렴하지 않았다.
  - 조사하지 않은 조건: 원인은 Seed 0, 4096 spp, Profile, 1920x1080 과 두 시점의 light 에서만 조사했다. sample 하나로 설명되지 않는 흰 점은 복도에서 89 개 (47 개), start 에서 92 개 (83 개) 이고, 원인을 보지 않았다. start 시점의 Radiance 에서 Lambert 의 흰 점은 재지 않았다.
- infra 검증 5-4 ~ 5-7 의 mode: PT 에 그 문제로 보이는 결과가 나올 때 (323e185)
- albedo 1 의 furnace 에서 16384 번 넘게 반사하고도 escape 하지 못한 path 가 어디에 갇혔는지 보는 일 (a5060e3): 작업 13 이나 PT 에서 leak, self-hit 로 보이는 결과가 나올 때. Indirect Irradiance 의 white furnace 에서도 16 spp 중 한 pixel 의 sample 하나가 65536 번 안에 escape 하지 못했다 (a93d5bb 의 check-indirect-irradiance.py [I1]).
- Shading Lambert 의 PT export 가 같은 build 에서도 실행마다 rgb 가 4 ulp 까지 다른 원인: Lambert 의 bit 재현에 의존하는 확인 (check-gltf-brdf.py 의 [S5]) 이 실패할 때. d05ec6f 를 두 번 띄워 같은 조건으로 쌓은 export 에서 pixel 의 0.4 ~ 1.3% 가 달랐고, glTF 는 같았다. check-indirect-irradiance.py 의 [K1] 은 이 차를 허용한다. PathTracerCS.hlsl 의 LAMBERT_SHADING 주석이 적은 반올림 차이도 같은 원인일 수 있다 (추측).
- 작업 11 의 check 를 처음 돌릴 때 Indirect Irradiance 의 white furnace (Max Bounces 65536) 를 쌓다가 app 이 exit reason 없이 끝난 원인: 다시 일어날 때. 같은 조건을 손으로 쌓을 때와 check 를 다시 돌릴 때는 재현되지 않았다.
- PT panel 의 Export button: script 없이 사람이 PT 의 값을 export 할 일이 생길 때. 지금은 harness 의 `capture pt` 로만 export 한다 (2b33c11).
- Engine Tuning 의 Graphics/SSAO 항목을 없애는 일: ModelViewer 는 SSAO 를 돌리지 않지만 engine 의 SSAO module 이 남아 있어 효과 없는 항목이 보인다. legacy Sponza renderer 를 지울 때 (35e8e58)
- NaN 이나 Inf 라서 버린 sample 수를 panel 에 보이는 일: 작업 8 부터 PT 에 NaN 이나 Inf 가 생겨 사람이 실행 중에 봐야 할 때. 지금은 export 의 a 로만 센다 (2b33c11).
- normal map 이 없는 material 의 shading normal 을 vertex normal 그대로 두는 일: 작업 13 (a) 의 Cornell box 처럼 normal map 이 없는 장면을 정답으로 쓰기 전. 지금은 그런 material 에도 기본 flat normal texture (8 bit 의 128, 128, 255) 를 적용해 PT 의 shading normal 이 0.3° 쯤 기울고, check-directional-light.py 의 [P4] 가 그 오차를 허용한다 (c8610b9).
- vertex normal 을 float 로 담는 일: shading normal 의 오차가 측정에 보일 때. 지금은 R10G10B10A2 로 담아 성분마다 1/1023 까지 다르다 (c8610b9).
- 일반 game engine 처럼 transform 을 편집하는 일 (translate 의 Local 과 World, Inspector 의 수치 입력, snap, undo): 이 로드맵 밖에서 따로 한다. 지금은 rotate 만 Local 과 World 를 고르고, 수치는 harness 의 light set 으로만 넣는다 (665bea1).
