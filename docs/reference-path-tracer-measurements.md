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
