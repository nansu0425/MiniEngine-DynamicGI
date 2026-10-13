# Roadmap

기간은 2026-10-12(월)부터 2026-12-04(금)까지 8주다.

| 주차 | 기간 | 목표 |
|---|---|---|
| 1 | 10/12 ~ 10/16 | 기준선 측정 수단, DXR 시작 |
| 2 | 10/17 ~ 10/23 | ray tracing 기반 |
| 3 | 10/24 ~ 10/30 | DDGI, reference path tracer |
| 4 | 10/31 ~ 11/06 | artifact 측정과 분석 |
| 5 | 11/07 ~ 11/13 | artifact 보완 |
| 6 | 11/14 ~ 11/20 | artifact 보완 |
| 7 | 11/21 ~ 11/27 | artifact 보완, 기능 동결 |
| 8 | 11/28 ~ 12/04 | 버퍼 |

## 1주차: 기준선 측정 수단, DXR 시작

- 카메라 preset 을 저장하고 불러와 같은 시점의 screenshot 을 찍는다.
- pass 별 GPU time 을 표시한다.
- 검증 scene 을 정한다. Sponza 와, 빛이 새는지 판별할 수 있는 밀폐 공간 scene 이다.
- device 를 `ID3D12Device5` 로 만들고, shader 를 `RayQuery` 를 쓸 수 있는 shader model 로 compile 한다.

완료 기준: 카메라 preset 별 baseline 이미지와 frame time 을 다시 뽑을 수 있다.

## 2주차: ray tracing 기반

- `Model` 의 mesh 로 BLAS 를, scene instance 로 TLAS 를 만든다.
- compute shader 에서 `RayQuery` 로 ray 를 쏜다.
- hit 지점에서 normal 과 material 의 평균 albedo 를 읽는다.
- hit 지점의 direct lighting 을 sun 과 shadow ray 로 계산한다.

완료 기준: 카메라 ray 로 그린 normal, albedo debug view 가 raster 결과와 겹친다.

## 3주차: DDGI, reference path tracer

- probe grid 와 probe 별 ray trace
- irradiance, distance octahedral atlas 와 border 갱신
- hysteresis blending
- Chebyshev visibility
- multi bounce (hit 지점에서 이전 frame 의 volume 을 sampling)
- Forward+ opaque pixel shader 에서 indirect diffuse 로 sampling
- probe 시각화 debug view
- 2주차 hit shading 을 재사용하는 누적 방식 reference path tracer

완료 기준: 카메라 preset 별로 GI off, DDGI, reference 이미지를 나란히 뽑을 수 있다.

## 4주차: artifact 측정과 분석

3주차 DDGI 결과에서 artifact 를 찾아 유형별로 정리한다. 유형마다 아래를 만든다.

- 그 artifact 만 드러나는 재현 시나리오 (scene, 카메라 preset, 광원이나 물체의 움직임)
- 자동으로 뽑을 수 있는 지표 (reference 대비 오차, 수렴 frame 수, frame 간 변화량 등)
- 원인 분석과 보완 기법 후보

완료 기준: artifact 유형별 재현 시나리오와 지표 값이 있고, 5 ~ 7주차에 보완할 대상이 정해져 있다.

## 5 ~ 7주차: artifact 보완

4주차에 정한 대상을 보완한다. 주차별 대상은 4주차가 끝나면 이 문서에 채운다.

| 주차 | 대상 artifact | 적용 기법 |
|---|---|---|
| 5 | | |
| 6 | | |
| 7 | | |

각 보완은 아래를 갖춰야 끝난다.

- 4주차 재현 시나리오에서 적용 전후 이미지
- 적용 전후 지표 값
- 추가 비용 (GPU time, memory)

7주차가 끝나면 기능을 동결한다.

## 8주차: 버퍼

1 ~ 7주차에서 끝내지 못한 작업을 끝낸다. 새 기능은 넣지 않는다.
