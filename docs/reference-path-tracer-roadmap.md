# Reference Path Tracer 로드맵

DDGI 와 RT Reflection 의 정답 image 를 만드는 path tracer (PT) 를 inline RayQuery compute pass 로 만든다. 작업 branch 는 `feat/reference-path-tracer` 이고, 아래 작업을 끝내면 `main` 에 merge 한다. 10/30 WEEK 3 에 발표한다.

이 문서는 기한 안에 할 작업과 그 순서만 정한다. 작업의 설계와 완료 기준은 그 작업을 시작할 때 코드와 spec 을 보고 정한다. 미리 정한 세부는 작업을 시작하면 달라지고, 그때마다 이 문서를 코드와 맞추는 비용이 들기 때문이다. 끝난 작업의 확인은 그 작업이 남긴 script 가, 다시 재면 바뀌거나 다시 재기 비싼 수치는 [측정 기록](reference-path-tracer-measurements.md) 이, 코드보다 오래 남아야 할 이유는 코드 주석이 갖는다. 끝난 작업이 뒤로 넘긴 일은 그 일을 할 작업이나 "뒤로 미룬 일" 에 적고, 끝난 작업에는 commit 만 적는다.

def659b 의 이 문서에는 미리 조사해 둔 설계 (출력, BRDF, light, sampling, 누적, export 와 비교) 와 작업별 완료 기준이 있다. 작업을 시작할 때 출발점으로 참고하되 그대로 따르지 않는다.

## 목표

- glTF metallic-roughness material 을 bias 없이 path tracing 한 정답을 만든다. PT 와 PT 가 쓰는 engine 코드는 README "Rendering 기준" 을 따른다.
- 정답이 맞다는 것을 furnace, BRDF 단위의 test, 수렴 측정, 외부 renderer 와의 비교로 보인다. DDGI 와 PT 의 비교 수치는 외부 renderer 와 맞춘 작업 13 이 통과한 뒤에 믿는다.
- DDGI 가 같은 시점에서 비교할 양을 정의한다. DDGI 의 오차를 DDGI 가 계산하지 않는 path 의 몫과 계산하는 path 의 정확도로 나눌 수 있어야 한다.
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
11. Indirect Irradiance: DDGI 와 비교할 양을 누적하는 출력을 더한다.
12. 수렴과 측정: 고정 시점에서 수렴과 bias 를 재고 ground truth 의 spp 를 정한다.
13. Cornell box 와 외부 renderer: (a) 장면을 만들어 load 하고, (b) 외부 renderer 로 그린 image 와 비교한다. furnace 가 잡지 못하는 leak, self-hit, visibility, 방향 분포의 오류를 여기서 본다. `-model` 로 연 장면은 orbit camera 라서 측정 조건의 줄로 Restore 할 수 없으므로 (39d9b61), (a) 가 그 장면에서도 camera 를 되돌리게 한다.

## 일정

작업마다 끝낼 기한만 정한다. 작업에 드는 시간은 해 봐야 알고 같은 작업도 기한에 따라 들이는 시간이 달라지므로, 작업 시간은 추정하거나 기록하지 않는다. 날짜만 적은 기한은 그날이 끝날 때다. 작업은 기한 안에 끝내고 임의로 뒤로 미루지 않는다.

| 기한 | 끝낼 작업 |
|---|---|
| 10/24 | 끝남: 1, A, B, harness 의 D3D12 log 수집 (573dded) 과 그 log 로 찾은 Lighting 수정 (e8a2352), 문서 정리 |
| 10/25 | 끝남: 2, 3, 4 |
| 10/26 | 끝남: 5, 6, C, 7 |
| 10/27 | 끝남: 8, 9, 10 |
| 10/28 | 11, 12, 13 (a) |
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
- DDGI 가 비교할 양과, DDGI 가 계산하지 않는 path 의 몫: 11, 12
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
- glTF BRDF 의 coupling table 을 grazing 에서 더 촘촘히 두는 일: E_s 의 보간 오차가 μ ≥ 0.03 에서 3.0e-4, 그 아래에서 7.3e-4 까지이고 energy 가 느는 쪽은 3.4e-4 까지다 (`scripts/gltfbrdf.py`). 작업 12 의 bias 가 grazing 에 몰릴 때
- 방향에 따라 다른 sky 와 그 importance sampling, MIS: 상수 sky 로는 볼 수 없는 결과가 필요해질 때. 그때 맞는 sky 를 구하거나 만든다.
- 상수 Sky Light 의 NEE 와 MIS: 작업 12 가 sky 에서 오는 빛 때문에 수렴이 느리다고 잴 때. 상수 sky 의 방향을 cosine 으로 고르면 pdf 가 Lambert 의 BRDF sampling 과 같아서 얻는 것이 적다. Sponza 의 실내에서는 sky 가 courtyard 로만 보이므로, 그 구멍 쪽을 고르는 portal sampling 이 있어야 효과를 볼 것이다 (추측).
- emissive triangle 의 NEE: 작은 emitter 가 수렴을 막을 때
- stratified sampler, blue noise: 수렴 시간이 문제가 될 때
- glTF 의 caustic 이 남기는 흰 점을 줄이는 일: 작업 12 가 아래 시점에서 수렴을 재고, 흰 점이 수렴 목표를 막을 때. 햇빛이 roughness 가 0 에 가까운 texel 에서 반사되어 다른 면을 비추는 path 는 그 texel 의 NEE 로만 드물게 찾으므로, 아주 큰 sample 이 어두운 곳에 흰 점으로 남는다 (아래 값에서 한 추론). 작업 8 을 끝낼 때 복도 시점 (pose `-1479.30859 211.686859 395.723236 -1.53974497 -0.124806166`, light direction `0,0.970682025,-0.24036777`, intensity 4, Max Bounces 64, Russian roulette on, 4096 spp) 에서 잰 값:
  - 흰 점 (8 이웃의 최대보다 0.004 넘게 밝고 2 배 이상인 pixel) 은 glTF 344 개, Lambert 27 개다. Seed 를 바꾸면 같은 자리는 42 개이고 평균은 같다.
  - sky 만 켜면 거의 사라지고, Max Bounces 1, 2, 3 (1024 spp) 에서 42, 108, 324 개다.
  - 흰 점 하나를 만든 sample 의 값은 중앙값 27, 최대 8281 이다. Lambert 의 sample 은 약 intensity / π 를 넘지 못한다.
  - 줄이는 방법의 후보는 directional light 의 angular size 와 MIS (범위 밖에서 옮겨야 한다) 와 α 하한을 올리는 일이다. sample 값을 자르는 일은 bias 가 생겨 정답에 맞지 않는다.
- infra 검증 5-4 ~ 5-7 의 mode: PT 에 그 문제로 보이는 결과가 나올 때 (323e185)
- albedo 1 의 furnace 에서 16384 번 넘게 반사하고도 escape 하지 못한 path 가 어디에 갇혔는지 보는 일 (a5060e3): 작업 13 이나 PT 에서 leak, self-hit 로 보이는 결과가 나올 때
- PT panel 의 Export button: script 없이 사람이 PT 의 값을 export 할 일이 생길 때. 지금은 harness 의 `capture pt` 로만 export 한다 (2b33c11).
- Engine Tuning 의 Graphics/SSAO 항목을 없애는 일: ModelViewer 는 SSAO 를 돌리지 않지만 engine 의 SSAO module 이 남아 있어 효과 없는 항목이 보인다. legacy Sponza renderer 를 지울 때 (35e8e58)
- NaN 이나 Inf 라서 버린 sample 수를 panel 에 보이는 일: 작업 8 부터 PT 에 NaN 이나 Inf 가 생겨 사람이 실행 중에 봐야 할 때. 지금은 export 의 a 로만 센다 (2b33c11).
- normal map 이 없는 material 의 shading normal 을 vertex normal 그대로 두는 일: 작업 13 (a) 의 Cornell box 처럼 normal map 이 없는 장면을 정답으로 쓰기 전. 지금은 그런 material 에도 기본 flat normal texture (8 bit 의 128, 128, 255) 를 적용해 PT 의 shading normal 이 0.3° 쯤 기울고, check-directional-light.py 의 [P4] 가 그 오차를 허용한다 (c8610b9).
- vertex normal 을 float 로 담는 일: shading normal 의 오차가 측정에 보일 때. 지금은 R10G10B10A2 로 담아 성분마다 1/1023 까지 다르다 (c8610b9).
- 일반 game engine 처럼 transform 을 편집하는 일 (translate 의 Local 과 World, Inspector 의 수치 입력, snap, undo): 이 로드맵 밖에서 따로 한다. 지금은 rotate 만 Local 과 World 를 고르고, 수치는 harness 의 light set 으로만 넣는다 (665bea1).
