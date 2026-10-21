# Ray Query Infra 로드맵

Reference Path Tracer, DDGI, RT Reflection 이 같이 쓰는 inline ray tracing (RayQuery) 기반을 만든다. 작업 branch 는 `feat/ray-query-infra` 이고, 아래 작업을 끝내면 `main` 에 merge 한다.

## 목표

- closest hit: ray 가 처음 맞는 surface 의 위치, normal, UV, material 을 알려 준다.
- visibility: 두 점 사이가 막혔는지 알려 준다.

hit 지점의 조명 계산은 Reference Path Tracer 에서 한다.

## 작업

1. AS build: AS 전용 buffer type 을 만든다. Mesh 마다 BLAS 를 load 때 한 번 build 하고, TLAS 는 매 frame build 한다.
2. closest hit 와 debug view: camera ray 로 hit t, instance, front face 를 화면에 표시한다.
3. HitSurface: shader 에서 VB, IB, material, texture 를 읽어 normal, UV, base color 등을 복원하고 debug view 에 표시한다.
4. alpha test 와 visibility: alpha test material 을 ray 도 통과하게 한다. visibility query 로 sun shadow 를 debug view 에 그린다.
5. 검증: Reference Path Tracer 를 만들기 전에 infra 만 따로 검증한다. PT 가 생기면 self-intersection 이나 NaN 이 만든 결과가 BRDF, sampling 의 bug 와 구별되지 않는다. 아래 순서로 하고, 각 검증은 화면의 heatmap 과 수치로 결과를 낸다.
   1. GBV: Debug build 를 `-gpu_debug 1` 로 VS debugger 에서 실행하고 Output 창의 D3D12 message 를 본다. D3D12 message 는 실행을 멈추지 않는다. GBV 는 실행된 shader 경로만 검사하므로 모든 debug view, Show Single-Sided Back Faces 의 on/off, window 크기 변경, sun 회전, leaf 와 chain 이 보이는 화면을 거친다. ERROR 와 CORRUPTION 이 없어야 한다.
   2. 검증 pass 의 기반: 3 ~ 5 를 mode 로 갖는 compute pass 를 만든다.
      - camera ray 를 raster 와 같은 sample 위치로 쏜다. raster 는 viewport 를 jitter 만큼 옮기고 TAA 를 꺼도 jitter 가 0.5 라서, pixel 중심을 쏘는 지금의 debug view 는 raster 와 반 pixel 어긋난다. debug view 도 이 camera ray 를 쓴다.
      - debug view 와 검증 mode 는 같은 overlay 에 그리므로 한 panel 의 view 하나로 고른다. view 목록을 Debug 와 Validation 으로 나누고, 하나만 고를 수 있게 UI 가 강제한다. 검증 mode 의 ray 는 PT 처럼 `kCullSingleSidedBackFaces` 를 따르므로, debug view 에서만 쓰는 option 은 숨기지 않고 비활성으로 보여 준다.
      - pixel 마다의 결과는 overlay 에 heatmap 으로 그리고, stats buffer 에 category 별 count, log2 scale 의 error histogram, max 로 모은다.
      - panel 의 Capture 로 stats 를 읽어 비율과 p50, p99, max 를 보여 준다. Log 는 수치를 camera pose, sun 방향, TAA, 해상도와 함께 한 줄로 찍는다.
      - 고친 뒤 같은 곳에서 다시 잴 수 있게, panel 에 붙여 넣은 Log 한 줄로 camera pose, sun 방향, TAA 를 되돌린다. app 을 다시 실행해도 되돌릴 수 있어야 한다. 해상도와 TAA 를 켰을 때의 jitter 는 되돌릴 수 없으므로 Log 와 다르면 panel 에 표시한다.
      - 집계가 맞는지는 정답을 아는 Ray Hit mode 로 확인한다. hit 와 miss 의 합이 pixel 수와 같고, 멈춘 화면에서 Capture 를 반복하면 같은 값이 나와야 한다. 3 의 category 도 합이 pixel 수와 같아 같은 확인이 되므로 3 에서 Ray Hit mode 를 지운다.
   3. depth 비교: pixel 마다 raster depth 와 ray depth (`t * dot(D, forward)`) 를 비교한다. ray 의 TMin, TMax 는 raster 의 near, far plane 에 맞춘다.
      - 둘 다 miss, raster 만 hit, ray 만 hit, depth 일치, depth 불일치로 나눈다.
      - 불일치한 pixel 은 `RAY_FLAG_FORCE_OPAQUE` 로 다시 쏴서 결과가 바뀌면 alpha test 불일치로 따로 센다. raster 의 cutout 은 mip chain 을, ray 는 mip 0 을 읽으므로 alpha test 불일치는 보고만 한다.
      - alpha test 와 무관한 불일치는 raster 의 subpixel snap 때문에 edge 에서 생기는 극소수만 허용한다.
   4. hit 불변식: camera ray 와 5 의 cosine hemisphere ray 가 맞은 곳에서 HitSurface 를 만들고 다음을 어긴 수를 센다. NaN/Inf 와 facing 불일치는 없어야 한다.
      - `surface.position` 과 `O + t * D` 의 거리. hardware 의 t 만 쓰는 3 과 달리 shader 가 vertex 를 읽는 경로를 검증한다. 이 거리의 분포는 `SurfaceRayOrigin` 의 offset 이 position 오차를 덮는지도 보여 준다.
      - `frontFace` 와, 읽은 vertex 의 `cross(p1 - p0, p2 - p0)` 로 계산한 facing 의 일치
      - geometryNormal, shadingNormal 의 길이가 1 이고, baseColor, metallic, roughness 가 [0, 1] 안에 있는지
      - HitSurface 의 모든 값의 NaN/Inf
   5. secondary ray: primary hit 에서 `SurfaceRayOrigin` 으로 출발해 cosine hemisphere, sun, grazing 방향으로 쏘고 방향별로 다음을 센다. culling 은 PT 가 쓸 `kCullSingleSidedBackFaces` 다. cosine hemisphere 와 sun 방향에서는 모두 없어야 하고, grazing 방향은 따로 보고한다.
      - self-hit: 출발한 triangle 에 다시 맞는다.
      - 뚫림: origin 에서 `-geometryNormal` 로 쏜 ray 가 offset 거리 안에서 맞지 않는다. origin 이 얇은 면을 뚫고 나갔다는 뜻이다.
      - closest hit 와 visibility 의 불일치: closest hit 가 t 인 ray 에서 `IsOccluded` 가 TMax `t(1 - ε)` 에 true 거나 `t(1 + ε)` 에 false 다. miss 한 ray 에서 true 다.
   6. TLAS build 의 GPU time: Release build 에서 GBV 를 끄고 profiler 의 Build TLAS 를 읽어 instance 수, GPU 와 함께 남긴다.
   7. GBV 다시: 1 을 검증 pass 의 mode 까지 넣어 다시 돌린다. secondary ray 가 camera 에 보이지 않던 mesh 의 material, texture 까지 읽기 때문이다.

   threshold 는 처음 잰 분포를 보고 정한다. 정한 threshold 와 6 의 측정값은 그 작업의 commit message 에 남긴다.

## 범위 밖

- 조명 (sun, sky, BRDF)
- skinned mesh, BLAS compaction, TLAS refit, 여러 ModelInstance
- 투명한 물체 (alpha blend material). 투명한 물체를 shading 할 때 ray 가 어떻게 볼지 정한다.
- glTF node transform 의 음수 determinant. glTF 는 이때 winding 이 바뀐다고 정하지만 DXR 은 facing 을 object space 에서 정해 instance transform 이 바꾸지 않으므로, mirror 된 single-sided mesh 는 ray 가 반대 면을 cull 한다. Sponza 에는 그런 node 가 없다.

## 뒤로 미룬 검증

PT 전에 하지 않는 검증이다. 그 검증이 필요한 작업을 시작하기 전에 한다.

- Specular GI: raster depth 로 복원한 위치에서 출발한 ray 의 self-intersection
- DDGI: 움직이는 instance 에서 매 frame build 한 TLAS 의 depth 비교, ray 종류별 trace 처리량, TLAS 의 PREFER_FAST_TRACE 와 PREFER_FAST_BUILD 비교
- 두 점 사이의 visibility 를 쓰는 조명 (point light, area light): A 에서 B, B 에서 A 로 본 visibility 의 대칭성
- PT 와 raster 의 차이가 material 에서 오는지 가려야 할 때: HitSurface 와 raster 의 attribute 비교
- Sponza 밖의 asset: Khronos glTF-Sample-Assets 로 작업 5 의 3 ~ 5 를 반복
- 검증 반복이 잦아질 때: command line 실행, report, script 로 자동화
