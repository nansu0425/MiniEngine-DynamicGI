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
5. 검증: ray depth 와 raster depth 를 비교하고, GPU-based validation 을 돌리고, TLAS build 의 GPU time 을 확인한다.

## 범위 밖

- 조명 (sun, sky, BRDF)
- skinned mesh, BLAS compaction, TLAS refit, 여러 ModelInstance
