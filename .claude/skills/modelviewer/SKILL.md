---
name: modelviewer
description: ModelViewer 를 build, run, drive, screenshot, verify 하는 harness (scripts/viewer.py). 코드를 고친 뒤 실제 app 에서 동작을 확인할 때, app 을 띄워 camera, 설정, panel 을 명령으로 바꾸고 화면을 capture 해 볼 때, crash 와 hang 을 판정할 때 쓴다.
---

# ModelViewer harness

진입점은 `python scripts/viewer.py` 하나다. 각 subcommand 가 무엇을 하는지는 `--help`, app 명령은 `do help`, 명령의 protocol 은 `MiniEngine/Core/Automation/ControlChannel.h`, 종료 이유의 뜻은 `MiniEngine/Core/Automation/UnattendedRun.h`, app flag 는 `MiniEngine/readme.md` 에 있다. 여기에는 agent 가 harness 를 운용하는 법만 적는다.

## 쓰는 범위

- app 을 직접 실행하지 않고 언제나 `start` 로 띄운다. 그래야 사용자의 설정 파일이 남는다 (`start --help`).
- harness 로 아직 볼 수 없는 것은 사용자에게 확인을 부탁한다: HDR (ST.2084) present, DPI 변경, 실제 mouse 와 keyboard 입력 경로, 사용자가 보는 최종 화면, D3D12 debug layer 의 message (아직 log 에 남지 않는다).
- mouse 와 keyboard 를 흉내 내는 명령은 아직 없다. ImGui 조작은 같은 일을 하는 명령 (`settings`, `panel`, `ui`, `validation`, `restore`) 으로 한다.

## 실행 loop

1. `start` 로 띄운다. 기능 확인은 기본값 Debug 로, 시간은 `--config Profile` 로 잰다.
2. `do "<명령>" "<명령>" ...` 로 조작한다. 서로 기다릴 필요가 없는 명령은 한 번의 `do` 에 묶는다.
3. `shot display|scene|overlay` 로 capture 하고 출력된 view PNG 를 Read 로 본다.
4. `stop` 으로 끝낸다.

- 나중에 비교할 shot 은 `MiniEngine/ModelViewer/Harness/kept/` 로 복사해 둔다. `runs/` 의 run 은 뒤의 `start` 와 `selftest` 가 지울 수 있다.
- 사용자가 직접 띄운 ModelViewer 와 동시에 실행하면 `.mini` 와 DDS cache 쓰기가 겹칠 수 있다.
- `--model` 로 띄우면 orbit camera 라서 `pose`, `describe`, `restore` 가 error 를 돌려준다.

## capture 고르기

- display: UI 흐름과 panel 내용을 볼 때 쓴다.
- scene: 수치는 원본 DDS 로 비교한다. view PNG 는 고정 exposure 로 만든 보기용이다.
- overlay: debug view 와 heatmap 만 볼 때 쓴다.
- 수치를 재는 shot 은 `ui hide` 뒤에 찍는다. UI 가 보이면 scene 크기가 dock 배치를 따른다.

## 결정성

- 시작 상태는 `scripts/harness-baseline/` 이고, 값을 고른 이유는 그 파일의 주석에 있다.
- 같은 상태에서 연달아 찍은 scene 은 bit 가 같다 (selftest 가 확인한다).
- TAA 를 꺼도 engine 이 매 frame TAA history 를 섞는다 (`TemporalEffects::ResolveImage` 가 `EnableTAA` 와 관계없이 `ApplyTemporalAA` 를 부른다). 그래서 camera 를 옮기면 scene 이 수백 frame 동안 바뀐다. 옮긴 뒤 240 frame 에는 최대 0.125, 360 frame 에는 최대 0.03 의 차이가 남았다. viewer.py 의 `SETTLE_FRAMES` 는 이 측정으로 정했다. camera 를 옮기거나 `restore` 한 뒤 수치를 비교할 scene 은 `frames <SETTLE_FRAMES>` 뒤에 찍는다.
- 기다려도 같은 pose 의 scene 이 같아지지는 않는다. 움직였다가 같은 pose 로 돌아와 600 frame 을 기다린 scene 은 움직이기 전과 pixel 의 절반 이상이 다르다 (1920x1080 에서 1078985~1196303 pixel, 52~58%, 최대 0.25 를 쟀다). 그래서 같은 명령 순서로 찍은 이미지끼리만 비교한다. 코드의 전과 뒤를 비교할 때는 두 실행에서 같은 명령 순서를 쓴다. selftest 의 INFO 줄이 이 차이를 매번 잰다.

## token 규칙

- view PNG 만 Read 한다. 원본 PNG 와 DDS 는 Read 하지 않는다.
- 일부만 보면 되면 `--crop X,Y,W,H` (원본 pixel) 를, 구도만 보면 `--max-width 640` 을 쓴다.
- 최근 몇 장만 보고, 이미 본 shot 은 다시 Read 하지 않는다.

## 판정

- crash 와 ASSERT 는 dialog 없이 끝난다. `do`, `shot`, `stop` 이 종료 이유와 log 경로를 알려 준다. 원인은 log 의 끝부분과 종료 이유가 가리키는 module 의 offset 으로 찾는다.
- hang 이 보고되면 볼 것을 본 뒤 `stop` 으로 끝낸다.
- harness 자체가 의심되면 `selftest` 를 돌린다. app 을 여러 번 띄우고 끈다.

## 예

```
python scripts/viewer.py start
python scripts/viewer.py do "ui hide" "settings set Ray Tracing View: View=Validation: Depth Comparison" "frames 2" "validation capture"
python scripts/viewer.py shot overlay --max-width 640
python scripts/viewer.py stop
```
