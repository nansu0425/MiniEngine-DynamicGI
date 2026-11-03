# Reference Path Tracer 측정 기록

Reference Path Tracer 의 작업이 잰 값 중 다시 재면 바뀌거나 다시 재기 비싼 것을, 잰 조건과 함께 남긴다. 발표는 여기의 값을 쓴다. 이 기록과 code, commit message 의 "작업 N" 은 687700b 의 `docs/reference-path-tracer-roadmap.md` 에 있는 작업 N 이다.

- 항목은 그 값을 잰 script 가 덧붙인다. 덧붙이기만 하고, 고치거나 지우지 않는다.
- 코드 열은 잴 때의 HEAD 다. `+` 는 commit 하지 않은 변경이 있었다는 뜻이고, 그 변경까지 담은 코드는 그 줄을 더한 commit 이다.
- check script 가 다시 만드는 판정 결과는 남기지 않는다. 그 결과는 해당 commit 에서 script 를 다시 돌려 얻는다.
- 작업 4 까지의 수치는 각 작업의 commit message 에 있다.

## 작업 5: 1 spp 의 GPU time

`python scripts/check-lambert-path.py cost --record` 가 덧붙인다. 표에 없는 조건은 그 코드의 `cost` 가 정한다.

| 날짜 | 코드 | config | GPU | scene | Max Bounces | Russian roulette | frame | mean (ms) | min (ms) | max (ms) |
|---|---|---|---|---|---|---|---|---|---|---|
| 2026-10-26 | f980ee6+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 64 | on | 256 | 9.610 | 9.524 | 9.881 |
| 2026-10-26 | f980ee6+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 64 | off | 256 | 196.321 | 195.530 | 197.049 |

## 작업 6: shading normal 이 잃는 energy

`python scripts/check-path-tracer-export.py check --record` 가 덧붙인다. 앞의 세 값은 camera 에서 보이는 면에서 반사 한 번이 잃는 energy 의 비율이고, 재는 방법은 그 script 의 [N1] 에 있다. DDGI 는 이 손실을 만드는 규칙을 모르므로, DDGI 와 PT 의 차에 반사마다 이만큼이 섞인다. white furnace 의 손실은 albedo 1 의 path 가 escape 할 때까지 반사마다 잃은 것이 쌓인 값이다.

| 날짜 | 코드 | config | GPU | scene | spp | 반사 한 번의 평균 손실 | 손실이 있는 pixel 의 비율 | 그 pixel 의 평균 손실 | white furnace 의 평균 손실 |
|---|---|---|---|---|---|---|---|---|---|
| 2026-10-26 | e291d06+ | Debug | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 256 | 0.008838 | 0.374 | 0.02363 | 0.3766 |

## 작업 6: Russian roulette 의 pixel 단위 bias

`python scripts/check-path-tracer-export.py check --record` 가 덧붙인다. 장면을 Russian roulette off 와 on 으로, 각각 Seed 0 과 1 로 쌓아 `scripts/imagecompare.py` 의 r 을 잰다. bias 와 noise 는 r² = 1 + bias² / noise² 를 만족하는 rms 를 off 의 Seed 0 image 의 rms 로 나눈 값이다.

| 날짜 | 코드 | config | GPU | scene | Max Bounces | spp | r | bias | noise |
|---|---|---|---|---|---|---|---|---|---|
| 2026-10-26 | e291d06+ | Debug | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | 64 | 256 | 0.99995 | 0 | 0.282 |

## 작업 12: Indirect Irradiance 의 수렴

`python scripts/check-convergence.py check --record` 가 덧붙인다. 시점과 조건, e 와 기울기의 뜻은 그 script 에 있다. 1 spp 의 GPU time 은 Samples per Frame 16 의 frame 을 16 으로 나눈 값이다. 상위 0.1% 의 몫은 끝 spp 에서 두 Seed 의 차의 제곱합 중 가장 큰 0.1% pixel 이 차지하는 몫이고, glTF 에서는 흰 점의 몫이다. N_gt 까지의 시간은 reset 뒤 N_gt spp 를 쌓을 때까지 sample 을 더한 frame 들의 시간이다.

| 날짜 | 코드 | config | GPU | scene | 시점 | Shading | 1 spp GPU time (ms) | e(1024) | 끝 spp | e(끝) | 기울기 | 끝 1/4 의 기울기 | 상위 0.1% 의 몫 | N_gt | e(N_gt) | N_gt 까지 (s) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-10-28 | 668b4f0+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | start | Lambert | 9.581 | 0.0852 | 131072 | 0.00754 | -0.499 | -0.501 | 0.030 | 131072 | 0.00754 | 1264 |
| 2026-10-28 | 668b4f0+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | start | glTF | 9.658 | 0.2612 | 16384 | 0.3126 | -0.316 | 0.734 | 0.992 | 131072 | - | - |
| 2026-10-28 | 668b4f0+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | corridor | Lambert | 9.605 | 0.1124 | 262144 | 0.007066 | -0.498 | -0.499 | 0.092 | 131072 | 0.00998 | 1266 |
| 2026-10-28 | 668b4f0+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | corridor | glTF | 9.585 | 0.1532 | 16384 | 0.04733 | -0.432 | -0.437 | 0.613 | 131072 | - | - |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | sponza-color-bleeding | Lambert | 5.982 | 0.08642 | 131072 | 0.007666 | -0.499 | -0.500 | 0.035 | 131072 | 0.007666 | 793 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | sponza-color-bleeding | glTF | 6.063 | 0.0906 | 16384 | 0.02279 | -0.495 | -0.511 | 0.085 | 131072 | - | - |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | sponza-fireflies | Lambert | 7.707 | 0.1698 | 262144 | 0.0107 | -0.497 | -0.499 | 0.113 | 131072 | 0.01513 | 1018 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | sponza-fireflies | glTF | 7.745 | 0.4214 | 16384 | 0.09901 | -0.546 | -0.508 | 0.651 | 131072 | - | - |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | sponza-overhead-light | Lambert | 17.327 | 0.05869 | 65536 | 0.007344 | -0.500 | -0.499 | 0.017 | 131072 | - | - |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | sponza-overhead-light | glTF | 17.141 | 0.1302 | 16384 | 0.025 | -0.391 | -0.547 | 0.664 | 131072 | - | - |

## 작업 12: Indirect Irradiance 의 bias

`python scripts/check-convergence.py check --record` 가 덧붙인다. 비교하는 두 설정과 조건은 그 script 의 [C3] ~ [C5] 에 있다. d 와 b 와 그 상한의 식은 `scripts/imagecompare.py` 의 bias 에 있고, 모두 image 의 크기에 대한 비다.

| 날짜 | 코드 | config | GPU | scene | 기준 | 시점 | 비교 | spp | d | d 상한 | b | b 상한 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-10-28 | 668b4f0+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C3] | corridor | Max Bounces 64 대 4096 | 16384 | 0.00e+00 | 0.00e+00 | 0.00e+00 | 0.00e+00 |
| 2026-10-28 | 668b4f0+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C4] | corridor | Max Bounces 8 의 Russian roulette on 대 off | 16384 | -5.14e-06 | 1.62e-05 | 0.00e+00 | 4.52e-04 |
| 2026-10-28 | 668b4f0+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C5] | corridor | 131072 spp 한 run 대 8192 spp 16 run 의 float64 평균 | 131072 | 1.27e-05 | 5.83e-05 | 0.00e+00 | 1.18e-03 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C3] | sponza-color-bleeding | Max Bounces 64 대 4096 | 16384 | -1.36e-09 | 1.39e-09 | 0.00e+00 | 1.30e-06 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C4] | sponza-color-bleeding | Max Bounces 8 의 Russian roulette on 대 off | 16384 | -4.82e-06 | 1.14e-05 | 0.00e+00 | 2.55e-04 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C3] | sponza-fireflies | Max Bounces 64 대 4096 | 16384 | -8.42e-10 | 8.80e-10 | 0.00e+00 | 3.07e-06 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C4] | sponza-fireflies | Max Bounces 8 의 Russian roulette on 대 off | 16384 | 3.34e-06 | 3.68e-05 | 0.00e+00 | 1.35e-03 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C3] | sponza-overhead-light | Max Bounces 64 대 4096 | 16384 | -6.16e-11 | 8.85e-11 | 0.00e+00 | 4.27e-07 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C4] | sponza-overhead-light | Max Bounces 8 의 Russian roulette on 대 off | 16384 | 2.69e-08 | 1.45e-06 | 0.00e+00 | 2.12e-04 |
| 2026-11-01 | 926c737+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1920x1080 | [C5] | sponza-color-bleeding | 131072 spp 한 run 대 8192 spp 16 run 의 float64 평균 | 131072 | -1.51e-05 | 3.76e-05 | 0.00e+00 | 4.95e-04 |

## 작업 13: 외부 renderer 와의 비교

`python -I scripts/check-external-renderer.py check --record` 가 덧붙인다. 두 renderer 의 조건은 그 script 에 있다. d 와 b, 그 상한과 floor 의 식은 `scripts/imagecompare.py` 의 bias 에 있고, 모두 image 의 크기에 대한 비다. noise 는 각 renderer 의 한 run (Seed 하나) 의 상대 noise 로, 같은 파일의 e 다.

| 날짜 | 코드 | config | GPU | scene | 기준 | PT Max Bounces | Mitsuba max_depth | Mitsuba | spp | PT noise | Mitsuba noise | d | d 상한 | d floor | b | b 상한 | b floor |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X1] | 0 | 1 | 3.9.1 cuda_ad_rgb | 16384 | 1.12e-03 | 7.03e-04 | -2.51e-05 | 5.20e-05 | 2.69e-05 | 0.00e+00 | 4.60e-04 | 5.02e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X2] | 1 | 2 | 3.9.1 cuda_ad_rgb | 16384 | 5.64e-03 | 7.20e-04 | -2.28e-05 | 8.74e-05 | 6.45e-05 | 0.00e+00 | 5.28e-04 | 5.31e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X3] | 64 | 65 | 3.9.1 cuda_ad_rgb | 16384 | 7.69e-03 | 9.37e-04 | 1.31e-03 | 1.37e-03 | 6.19e-05 | 4.51e-04 | 7.20e-04 | 5.62e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X1] | 0 | 1 | 3.9.1 cuda_ad_rgb | 16384 | 1.12e-03 | 7.03e-04 | -2.51e-05 | 5.20e-05 | 2.69e-05 | 0.00e+00 | 4.60e-04 | 5.02e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X2] | 1 | 2 | 3.9.1 cuda_ad_rgb | 16384 | 5.64e-03 | 7.20e-04 | -3.32e-05 | 9.77e-05 | 6.45e-05 | 0.00e+00 | 5.28e-04 | 5.31e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X3] | 64 | 65 | 3.9.1 cuda_ad_rgb | 16384 | 7.70e-03 | 9.37e-04 | -2.43e-05 | 8.61e-05 | 6.18e-05 | 2.21e-04 | 6.04e-04 | 5.62e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X1] | 0 | 1 | 3.9.1 cuda_ad_rgb | 16384 | 1.12e-03 | 7.03e-04 | -2.51e-05 | 5.20e-05 | 2.69e-05 | 0.00e+00 | 4.60e-04 | 5.02e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X2] | 1 | 2 | 3.9.1 cuda_ad_rgb | 16384 | 5.64e-03 | 7.20e-04 | -5.30e-05 | 1.17e-04 | 6.45e-05 | 0.00e+00 | 5.01e-04 | 5.31e-04 |
| 2026-11-01 | d943ca7+ | Profile | NVIDIA GeForce RTX 4060 Laptop GPU | 1024x1024 | [X3] | 64 | 65 | 3.9.1 cuda_ad_rgb | 16384 | 7.70e-03 | 9.37e-04 | -9.36e-06 | 7.11e-05 | 6.18e-05 | 0.00e+00 | 5.56e-04 | 5.62e-04 |
