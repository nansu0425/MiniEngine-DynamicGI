# Reference Path Tracer 측정 기록

[로드맵](reference-path-tracer-roadmap.md) 의 작업이 잰 값 중 다시 재면 바뀌거나 다시 재기 비싼 것을, 잰 조건과 함께 남긴다. 발표는 여기의 값을 쓴다.

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
