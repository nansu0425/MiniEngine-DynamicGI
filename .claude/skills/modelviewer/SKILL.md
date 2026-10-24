---
name: modelviewer
description: ModelViewer 를 build, run, drive, screenshot, verify 하는 harness (scripts/viewer.py). 코드를 고친 뒤 실제 app 에서 동작을 확인할 때, app 을 띄워 camera, 설정, panel 을 명령으로 바꾸고 화면을 capture 해 볼 때, crash 와 hang 을 판정할 때 쓴다.
---

# ModelViewer harness

진입점은 `python scripts/viewer.py` 하나다. 각 subcommand 가 무엇을 하는지는 `--help`, app 명령은 `do help`, 명령의 protocol 은 `MiniEngine/Core/Automation/ControlChannel.h`, 종료 이유의 뜻은 `MiniEngine/Core/Automation/UnattendedRun.h`, log 의 D3D12 message 줄은 `MiniEngine/Core/Automation/D3D12MessageLog.h`, app flag 는 `MiniEngine/readme.md` 에 있다. 여기에는 agent 가 harness 를 운용하는 법만 적는다.

harness 로 할 수 없는 일을 만나면 그 일이 계획되어 있는지와 그 근거를 `docs/harness-roadmap.md` 에서 본다.

## 쓰는 범위

- app 을 직접 실행하지 않고 언제나 `start` 로 띄운다. 그래야 사용자의 설정 파일이 남는다 (`start --help`).
- OS 를 거쳐야만 보이는 것은 computer use 로 본다 (아래 computer use 절). computer use 를 쓸 수 없는 session 에서는 그것을 사용자에게 확인해 달라고 부탁한다.
- D3D12 debug layer 의 message 를 봐야 하면 Debug 로 띄운다. `start` 는 다른 config 에 `-debug 1` 을 주지 않으므로 debug layer 가 꺼져 message 자체가 생기지 않는다 (`start` 가 상태를 알린다).
- DXGI message 는 harness 로도 computer use 로도 볼 수 없으므로 사용자에게 확인을 부탁한다.
- mouse 와 keyboard 를 흉내 내는 명령은 아직 없다. ImGui 조작은 같은 일을 하는 명령 (`settings`, `panel`, `ui`, `validation`, `restore`) 으로 한다.

## 실행 loop

1. `start` 로 띄운다. 기능 확인은 기본값 Debug 로, 시간은 `--config Profile` 로 잰다.
2. `do "<명령>" "<명령>" ...` 로 조작한다. 서로 기다릴 필요가 없는 명령은 한 번의 `do` 에 묶는다.
3. `shot display|scene|overlay` 로 capture 한다. 출력된 view PNG 는 판정 절이 정한 경우에만 Read 로 본다.
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
- 두 명령 사이에는 1 frame 이상이 지나지만 몇 frame 인지는 timing 이 정한다 (명령을 실행하는 방식은 `ControlChannel.h`). `frames <n>` 도 n frame 이상을 기다릴 뿐이다.
- TAA 를 끄면 그다음 frame 부터 scene 이 그 frame 의 raster 만으로 정해진다. 같은 상태에서 찍은 scene 은 camera 를 옮겼다 되돌린 뒤에도, TAA 를 켰다 끈 뒤에도, TAA 의 Sharpness 를 바꾼 뒤에도 bit 가 같다 (selftest 가 확인한다).
- 1 frame 늦게 반영되는 것이 둘 있다. TAA 를 끈 frame 은 앞 frame 이 정한 jitter 로 그려지고, Exposure 를 바꾼 frame 의 display 는 바꾸기 전의 밝기다. viewer.py 의 `SETTLE_FRAMES` 는 이 지연을 명령 간격에 기대지 않고 덮는 frame 수다. 상태를 바꾼 뒤 수치를 비교할 capture 는 `frames <SETTLE_FRAMES>` 뒤에 찍는다.
- TAA 나 adaptive exposure 를 켜면 결과가 지난 frame 수에 따라 바뀌므로, 같은 명령 순서로도 실행마다 다를 수 있다. 수치를 비교할 capture 는 둘을 끄고 찍는다.

## token 규칙

- view PNG 만 Read 한다. 원본 PNG 와 DDS 는 Read 하지 않는다.
- 일부만 보면 되면 `--crop X,Y,W,H` (원본 pixel) 를, 구도만 보면 `--max-width 640` 을 쓴다.
- 최근 몇 장만 보고, 이미 본 shot 은 다시 Read 하지 않는다.

## 판정

- 상태 확인과 PASS/FAIL 판정은 text 와 수치로 한다. text 는 `status`, `describe`, `settings get` 과 명령의 응답이고, 수치는 image diff 와 비교 script 의 결과다. screenshot (computer use 의 screenshot 과 `shot` 의 view PNG) 을 해석해 판정하지 않는다.
- screenshot 은 수치로 좁힌 영역의 원인을 볼 때와 입력할 위치를 찾을 때만 쓴다.
- computer use 로 UI 를 조작했으면 그 결과도 조작 전과 뒤의 text 상태를 비교해 확인한다.
- text 로 읽을 수 없는 상태를 바꾸는 조작은 결과를 판정할 수 없다. 지금은 gizmo 의 위치와 mode 가 그렇다. 이런 조작의 결과는 사용자에게 확인을 부탁한다.
- D3D12 message 는 `stop` 의 요약과 `do status` 의 `d3d12_messages` 로 센다. 실행 중의 CORRUPTION, ERROR, WARNING 중 하나라도 0 이 아니면 FAIL 이다 (selftest 가 baseline 에서 셋 다 0 인 것을 확인한다). 종료 때는 `#274 LIVE_DEVICE` 하나만 나와야 하고, 그 밖의 message 는 device 보다 오래 남은 object 다 (selftest 가 baseline 에서 확인한다).
- 어느 명령이 message 를 냈는지는 그 명령 앞뒤에서 `do status` 의 `d3d12_messages` 를 비교해 찾는다. `status` 는 제한 없이 세지만 log 는 같은 ID 를 정해진 횟수까지만 줄로 남기므로 (`D3D12MessageLog.h`) `log mark` 로 나눈 구간에는 줄이 없을 수 있다.
- `start --gpu-validation` 이 무엇을 더 검사하는지는 `start --help` 에 있다.
- crash 와 ASSERT 는 dialog 없이 끝난다. `do`, `shot`, `stop` 이 종료 이유와 log 경로를 알려 준다. 원인은 log 의 끝부분과 종료 이유가 가리키는 module 의 offset 으로 찾는다.
- hang 이 보고되면 볼 것을 본 뒤 `stop` 으로 끝낸다.
- harness 자체가 의심되면 `selftest` 를 돌린다. app 을 여러 번 띄우고 끈다.

## computer use

- Windows 에서는 Claude desktop app 의 Code tab session 에서만 쓸 수 있고, CLI 에서는 쓸 수 없다. 도구는 `mcp__computer-use__*` 다.
- OS 를 거쳐야만 보이는 것을 확인할 때만 쓴다: 실제로 present 된 화면, HDR, DPI, 실제 입력 경로 (WM message, DirectInput 의 foreground 검사). 그 밖의 확인은 harness 명령으로 한다. harness 가 frame 단위로 결정적이고 비용도 싸다.
- app 은 이때도 `start` 로 띄운다. `open_application` 은 flag 없이 새 instance 를 띄워 사용자의 `editor.ini`, `imgui.ini` 를 덮어쓸 수 있으므로 쓰지 않는다.
- `request_access` 에는 이름 `ModelViewer` 가 아니라 `do status` 가 돌려주는 `exe` 경로를 준다. 이름으로는 찾지 못하고, 경로로는 full tier 로 승인된다. config 마다 exe 경로가 다르므로 config 를 바꾸면 다시 승인받아야 할 수 있다 (확인하지 않았다).
- screenshot 에는 D3D 영역도 그려지고 `shot display` 와 구도가 같다. `scale` 을 0.6 정도로 줄여 찍는다. click 좌표는 응답이 알려 주는 coordinate frame 기준이다. 허용하지 않은 app 은 가려지지만 File Explorer 창은 그대로 보인다.
- 조작 (결과는 판정 절에 따라 text 상태로 확인한다)
  - ImGui menu 와 panel 은 left click 으로 다룬다. F1 로 UI 를 숨겼다 되돌려도 dock 배치는 남는다.
  - 키를 누른 채 두려면 `hold_key` 를 쓴다.
  - gizmo 를 끌 때는 `mouse_move`, `left_mouse_down`, 0.2 s 간격의 `mouse_move` 몇 번, `left_mouse_up` 순서로 한다. `left_click_drag` 는 gizmo 를 고르기만 하고 끌지 못한다.
  - computer use 는 오른쪽과 가운데 button 을 누른 채 둘 수 없어서 camera 를 움직이지 못한다 (camera 조작법은 `MiniEngine/readme.md` 의 Controls). camera 는 `pose` 로 옮긴다.
- Esc 로 끝내면 정상 종료된다. Esc 는 DirectInput 으로 읽으므로 ModelViewer 창이 foreground 여야 한다. 끝난 뒤에는 `stop` 으로 instance 를 정리하고 log 요약을 본다.

## 예

```
python scripts/viewer.py start
python scripts/viewer.py do "ui hide" "settings set Ray Tracing View: View=Validation: Depth Comparison" "frames 2" "validation capture"
python scripts/viewer.py shot overlay --max-width 640
python scripts/viewer.py stop
```
