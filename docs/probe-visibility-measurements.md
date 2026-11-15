# Probe visibility 측정 기록

Probe visibility 작업이 잰 값 중 다시 재면 바뀌거나 다시 재기 비싼 것을, 잰 조건과 함께 남긴다.

- 항목은 그 값을 잰 script 가 덧붙인다. 덧붙이기만 하고, 고치거나 지우지 않는다.
- 코드 열은 잴 때의 HEAD 다. `+` 는 commit 하지 않은 변경이 있었다는 뜻이다.

## Distance Sharpness, distance 해상도, ray 수

`python scripts/measure-probe-distance.py --record` 가 덧붙인다. 표에 없는 조건은 그 script 가 정한다.

- N 은 Distance Texels per Side, rays 는 Rays per Probe, sharpness 는 Distance Sharpness 다.
- p50, p90, p99 는 texel 마다 distance mean 을 진짜 거리로 나눈 비의 백분위다. 진짜 거리는 texel 방향으로 쏜 ray 하나가 맞은 거리 (Probes Texel Distance) 다. 비가 1 보다 크면 mean 이 맞은 면보다 멀다.
- 1.1 배 초과는 그 비가 1.1 을 넘는 texel 의 비율이다.
- 변화 p90 은 수렴한 뒤 update 한 번에 바뀐 mean 을 진짜 거리로 나눈 값의 90 백분위다.
- Probe Trace 와 Probes Update 는 Profiler 의 GPU time 평균이다.

| 날짜 | 코드 | config | GPU | scene | N | rays | sharpness | p50 | p90 | p99 | 1.1 배 초과 | 변화 p90 | Probe Trace (ms) | Probes Update (ms) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
