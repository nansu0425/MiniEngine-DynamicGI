# Emissive sampling 측정 기록

Emissive sampling 작업이 잰 값 중 다시 재면 바뀌거나 다시 재기 비싼 것을, 잰 조건과 함께 남긴다.

- 항목은 그 값을 잰 script 가 덧붙인다. 덧붙이기만 하고, 고치거나 지우지 않는다.
- 코드 열은 잴 때의 HEAD 다. `+` 는 commit 하지 않은 변경이 있었다는 뜻이다.

## Emissive Samples per Probe 의 GPU time

`python scripts/measure-emissive-sampling.py --record` 가 덧붙인다. 표에 없는 조건은 그 script 가 정한다. GPU time 은 frame 마다 잰 scope 의 GPU time 의 평균이다. Probe Lighting 은 probe 갱신 전체의 scope 이고, Probe Trace, Probe Emissive Trace, Probes Update 를 포함한다.

| 날짜 | 코드 | config | GPU | scene | Probes | Rays per Probe | Emissive Samples per Probe | frame | Probe Trace (ms) | Probe Emissive Trace (ms) | Probes Update (ms) | Probe Lighting (ms) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-11-21 | 9f5a18e+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 8x8x8 | 256 | 0 | 64 | 0.110 | 0.000 | 0.415 | 0.524 |
| 2026-11-21 | 9f5a18e+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 8x8x8 | 256 | 32 | 64 | 0.110 | 0.014 | 0.439 | 0.562 |
| 2026-11-21 | 9f5a18e+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 8x8x8 | 256 | 64 | 64 | 0.110 | 0.022 | 0.456 | 0.587 |
| 2026-11-21 | 9f5a18e+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 8x8x8 | 256 | 128 | 64 | 0.110 | 0.036 | 0.534 | 0.680 |
| 2026-11-21 | 9f5a18e+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 8x8x8 | 256 | 256 | 64 | 0.110 | 0.065 | 0.663 | 0.839 |
