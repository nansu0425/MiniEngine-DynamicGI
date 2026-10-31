---
name: modelviewer
description: ModelViewer 를 build, run, drive, screenshot, verify 하는 harness (scripts/viewer.py). 코드를 고친 뒤 실제 app 에서 동작을 확인할 때, app 을 띄워 camera, directional light, 설정, panel, editor 의 선택을 명령으로 바꾸고 그 상태를 state 로 읽을 때, 화면에 무엇이 보이는지 pick 으로 볼 때, 화면을 capture 해 볼 때, crash 와 hang 을 판정할 때 쓴다.
---

# ModelViewer harness

진입점은 `python scripts/viewer.py` 하나다. 각 subcommand 가 무엇을 하는지는 `--help`, app 명령은 `do help`, 명령의 protocol 은 `MiniEngine/Core/Automation/ControlChannel.h`, 종료 이유의 뜻은 `MiniEngine/Core/Automation/UnattendedRun.h`, log 의 D3D12 message 줄은 `MiniEngine/Core/Automation/D3D12MessageLog.h`, app flag 는 `MiniEngine/readme.md` 에 있다. 여기에는 agent 가 harness 를 운용하는 법만 적는다.

harness 로 할 수 없는 일을 만나면 그 일이 계획되어 있는지와 그 근거를 `docs/harness-roadmap.md` 에서 본다.

## 쓰는 범위

- app 을 직접 실행하지 않고 언제나 `start` 로 띄운다. 그래야 사용자의 설정 파일이 남는다 (`start --help`).
- harness 로 볼 수 없는 것 (OS 를 거쳐야만 보이는 것과 DXGI message) 은 사용자 확인 목록에 넣어 보고한다 (아래 사용자 확인 목록 절).
- D3D12 debug layer 의 message 를 봐야 하면 Debug 로 띄운다. `start` 는 다른 config 에 `-debug 1` 을 주지 않으므로 debug layer 가 꺼져 message 자체가 생기지 않는다 (`start` 가 상태를 알린다).
- UI 와 mouse 로 바꾸는 상태는 같은 상태를 바꾸는 명령으로 바꾼다. gizmo 가 바꾸는 directional light 의 rotation 과 위치, icon click, Scene Hierarchy panel, W, E 와 scene 위의 gizmo toolbar 가 바꾸는 editor 의 선택, gizmo mode, rotate 의 축, Details panel 이 바꾸는 물체의 설정 (`editor get`, `editor set`), Scene menu 가 바꾸는 scene 과 Save (`scene save`), Engine Tuning 의 Reset to Default, Reset to Defaults, Reset All 이 바꾸는 설정 (`settings reset`) 도 그렇다. 명령은 `do help` 에 있다. gizmo 를 화면으로 보고 판단하거나 끌지 않는다 (근거는 `docs/harness-roadmap.md` 의 보류한 결정).

## 실행 loop

1. `start` 로 띄운다. 기능 확인은 기본값 Debug 로, 시간은 `--config Profile` 로 잰다. pass 의 GPU time 은 `profile` 명령으로 읽는다.
2. `do "state <path>"` 로 지금 상태를 읽고, `do "<명령>" "<명령>" ...` 로 조작한다. 서로 기다릴 필요가 없는 명령은 한 번의 `do` 에 묶는다.
3. 지금 무엇이 보이는지는 `pick grid` 로 본다. 대략의 구도를 보고 싶으면 `shot display|scene|overlay` 로 capture 해 view PNG 를 Read 한다.
4. `stop` 으로 끝낸다.

- 앞 명령의 응답에 있는 값을 뒤 명령에 넣거나 같은 순서를 되풀이하려면 명령을 파일에 적어 `run` 으로 보낸다. 형식은 `run --help` 에 있고, `scripts/harness-scenarios/acceptance.txt` 가 그 예다.
- 나중에 비교할 shot 은 `MiniEngine/ModelViewer/Harness/kept/` 로 복사해 둔다. `runs/` 의 run 은 뒤의 `start` 와 `selftest` 가 지울 수 있다.
- 사용자가 직접 띄운 ModelViewer 와 동시에 실행하면 `.mini` 와 DDS cache 쓰기가 겹칠 수 있다.
- `start` 는 `--scene` 이 없으면 측정용 Sponza 인 `scripts/harness-baseline/scenes/Sponza.ini` 를 연다. `MiniEngine/ModelViewer/Scenes/` 의 scene file 을 측정에 쓰지 않는 이유는 측정용 Sponza 의 머리말에 있다. Cornell box 의 측정용 scene file 도 같은 폴더에 있다. 다른 scene 은 `--scene <scene file>` 로 연다. 확인 script 가 만든 test scene 은 `write_scene_file` 이 쓴 scene file 로 연다. camera 는 늘 scene file 의 pose 로 시작하고, light 와 sky 도 scene file 의 값으로 시작한다. 실행 중에는 `scene open <scene file>` 로 다른 scene 을 연다. `scene save` 는 연 scene file 을 고치므로 `Scenes/` 와 `scripts/harness-baseline/scenes/` 의 scene file 에는 쓰지 않는다. 저장을 확인할 때는 Harness 아래에 복사한 scene file 을 연다.

## capture 고르기

- display: UI 흐름과 panel 내용을 볼 때 쓴다.
- scene: 수치는 원본 DDS 로 비교한다. view PNG 는 고정 exposure 로 만든 보기용이다.
- overlay: debug view 와 heatmap 만 볼 때 쓴다.
- pt: path tracer 의 값은 scene 이 아니라 이것으로 비교한다. scene 은 R11G11B10 으로 양자화한 값이다. 비교의 식은 `scripts/imagecompare.py` 에 있다.
- 수치를 재는 shot 은 `ui hide` 뒤에 찍는다. UI 가 보이면 scene 크기가 dock 배치를 따른다.

## 결정성

- 시작 상태는 `scripts/harness-baseline/` 이고, 값을 고른 이유는 그 파일의 주석에 있다.
- 두 명령 사이에는 1 frame 이상이 지나지만 몇 frame 인지는 timing 이 정한다 (명령을 실행하는 방식은 `ControlChannel.h`). `frames <n>` 도 n frame 이상을 기다릴 뿐이다.
- TAA 를 끄면 그다음 frame 부터 scene 이 그 frame 의 raster 만으로 정해진다. 같은 상태에서 찍은 scene 은 camera 를 옮겼다 되돌린 뒤에도, TAA 를 켰다 끈 뒤에도, TAA 의 Sharpness 를 바꾼 뒤에도 bit 가 같다 (selftest 가 확인한다).
- 1 frame 늦게 반영되는 것이 둘 있다. TAA 를 끈 frame 은 앞 frame 이 정한 jitter 로 그려지고, Exposure 를 바꾼 frame 의 display 는 바꾸기 전의 밝기다. viewer.py 의 `SETTLE_FRAMES` 는 이 지연을 명령 간격에 기대지 않고 덮는 frame 수다. 상태를 바꾼 뒤 수치를 비교할 capture 는 `frames <SETTLE_FRAMES>` 뒤에 찍는다.
- TAA 나 adaptive exposure 를 켜면 결과가 지난 frame 수에 따라 바뀌므로, 같은 명령 순서로도 실행마다 다를 수 있다. 수치를 비교할 capture 는 둘을 끄고 찍는다.
- Validation view 를 켠 채 명령 몇 개를 보내고 끈 직후에 찍은 scene 은, 끄기 전과 몇 pixel 이 다를 수 있다. Profile 의 baseline 에서는 늘 같은 36 pixel 이 최대 1/128 달랐고, 몇 번에 한 번 일어났으며, 3 frame 뒤에는 같았다. 원인은 확인하지 않았다. `state` 와 `pick` 을 더하기 전의 30152da 로 build 한 app 에서도 같은 pixel 수와 차이로 일어난다. Validation view 를 끈 뒤 수치를 비교할 capture 는 viewer.py 의 `VALIDATION_VIEW_SETTLE_FRAMES` 만큼 `frames` 로 기다린 뒤에 찍는다.

## token 규칙

- view PNG 만 Read 한다. 원본 PNG 와 DDS 는 Read 하지 않는다.
- 일부만 보면 되면 `--crop X,Y,W,H` (원본 pixel) 를, 구도만 보면 `--max-width 640` 을 쓴다.
- 최근 몇 장만 보고, 이미 본 shot 은 다시 Read 하지 않는다.
- `state` 전체는 수 KB 다. 필요한 group 만 `state <path>` 로 읽는다.

## 판정

- 판정의 SSOT 는 `state` 다. 상태 확인과 PASS/FAIL 판정은 `state` 와, 상태를 바꾼 명령이 응답에 담는 그 일부로 한다. 같은 값을 예전 형식으로 주는 `status`, `pose`, `describe`, `settings get` 은 `state` 와 같다 (selftest 가 확인한다). 수치는 image diff 와 비교 script 의 결과다.
- screenshot (`shot` 의 view PNG) 은 수치만으로 상황이 잡히지 않을 때 대략 파악하는 참고로 본다. screenshot 을 해석해 판정하지 않는다.
- image 와 `state` 가 다르면 `state` 를 믿고 차이의 원인을 찾는다. `state` 에 없는 항목이거나 rendering 의 bug 다. `shot` 이 image 옆에 남기는 state 파일이 그 image 를 그린 frame 의 `state` 이므로, image 에 보이는 것의 상태 (예: gizmo 가 translate 인지 rotate 인지) 는 image 가 아니라 그 파일에서 읽는다.
- 지금 무엇이 보이는지는 `pick grid` 의 칸마다 instance, material, 거리, 위치로 본다. 응답은 지도처럼 읽히게 행마다 그 넷이 각각 칸을 공백으로 이은 text 한 줄이고, miss 인 칸은 `-` 다. 한 pixel 은 `pick <x> <y>` 로 본다. mesh 와 material 은 이름 없이 index 만 있다 (.mini 에 이름이 없다).
- pick 의 좌표는 client pixel 이고 scene 영역 안이어야 한다. UI 가 보이면 dock 이 덮은 곳은 error 다. `state editor` 의 icon 화면 위치에 무엇이 있는지는 그 좌표를 반올림한 pixel 로 pick 한다. icon 위치는 ImGui 좌표라서 TAA 를 끄면 pick 의 pixel 격자와 반 pixel 어긋난다 (코드에서 추론했고 실행으로 확인하지 않았다).
- pick 의 `alpha_test` 가 true 면 raster 는 다른 것을 볼 수 있다. raster 의 cutout 은 mip chain 을, ray 는 mip 0 을 읽는다. 같은 pixel 의 raster depth 는 응답의 `raster` 에 있다.
- `validation fit` 은 그 view 의 `validation capture` 가 먼저 있어야 한다.
- D3D12 message 는 `stop` 의 요약과 `do status` 의 `d3d12_messages` 로 센다. 실행 중의 CORRUPTION, ERROR, WARNING 중 하나라도 0 이 아니면 FAIL 이다 (selftest 가 baseline 에서 셋 다 0 인 것을 확인한다). 종료 때는 `#274 LIVE_DEVICE` 하나만 나와야 하고, 그 밖의 message 는 device 보다 오래 남은 object 다 (selftest 가 baseline 에서 확인한다).
- 어느 명령이 message 를 냈는지는 그 명령 앞뒤에서 `do status` 의 `d3d12_messages` 를 비교해 찾는다. `status` 는 제한 없이 세지만 log 는 같은 ID 를 정해진 횟수까지만 줄로 남기므로 (`D3D12MessageLog.h`) `log mark` 로 나눈 구간에는 줄이 없을 수 있다.
- `start --gpu-validation` 이 무엇을 더 검사하는지는 `start --help` 에 있다.
- crash 와 ASSERT 는 dialog 없이 끝난다. `do`, `shot`, `stop` 이 종료 이유와 log 경로를 알려 준다. 원인은 log 의 끝부분과 종료 이유가 가리키는 module 의 offset 으로 찾는다.
- hang 이 보고되면 볼 것을 본 뒤 `stop` 으로 끝낸다.
- harness 자체가 의심되면 `selftest` 를 돌린다. app 을 여러 번 띄우고 끈다.

## 변경 전후 비교

- rendering 이나 그 입력을 바꾸는 작업은 코드를 고치기 전에 `snapshot save <이름>` 으로 고정 시점의 scene, display, path tracer 의 export 를 남기고, 고친 뒤 `snapshot compare <이름>` 으로 같은 shot 을 찍어 비교한다. scene 과 display 는 harness baseline 과 app 의 기본 효과 (TAA, motion blur, adaptive exposure) 두 설정에서 찍는다. PT 의 shot 은 Shading 과 Output 의 조합마다 쌓는다. 시점과 설정은 `viewer.py` 의 `SNAPSHOT_POSES`, `SNAPSHOT_SETTINGS`, `SNAPSHOT_PT_` 로 시작하는 상수에 있다.
- `CHANGED` 인 shot 은 sheet (`<shot>-sheet.png`) 를 보고 그 변화가 작업이 바꾸려던 것인지 밝힌다. 예상하지 않은 `CHANGED` 는 원인을 찾기 전까지 작업을 끝내지 않는다. 바뀐 정도는 report 의 수치로 말하고, sheet 는 어디가 바뀌었는지 보는 참고로 쓴다.
- app 의 기본 효과와 PT 의 Shading Lambert, DDGI Probe Hit 는 같은 code 에서도 실행마다 결과를 바꾼다. `save` 가 app 을 두 번 띄워 그 차이를 재 두고, `compare` 는 그보다 크게 바뀐 shot 만 `CHANGED` 로 표시한다. PT 의 차이는 두 실행으로 다 잴 수 없어서, Shading 마다 같은 code 의 차이로 보는 ulp 의 하한 (`SNAPSHOT_PT_RUN_ULPS`) 도 둔다. 그래서 `same` 은 같은 code 의 차이 안에서 바뀌었다는 뜻이다. `save` 가 찍는 `same code` 줄에서 바뀐 pixel 이 없는 raster 의 shot 과 PT 의 glTF shot 은, `compare` 에서 한 pixel 만 바뀌어도 `CHANGED` 다. 두 실행에서 바뀌는 shot 을 bit 단위로 비교해야 하면 작업의 check script 로 한다.
- `compare` 는 app 이 정상으로 끝나지 않거나 D3D12 CORRUPTION, ERROR, WARNING 이 있거나 NaN, Inf 를 그리면 exit 1 이다.
- camera 가 멈춘 shot 만 찍으므로 camera 가 움직일 때의 TAA 와 motion blur 는 보지 못한다.

## 사용자 확인 목록

harness 로 확인할 수 없는 것은 사용자가 직접 확인하도록 완료 보고에 목록으로 남긴다.

- 목록에 넣는 것은 실제로 present 된 화면, HDR, DPI, 실제 입력 경로 (WM message, DirectInput 의 foreground 검사, ImGui widget 을 mouse 로 다뤄야만 지나는 code. 예: 우클릭 menu), DXGI message 다. harness 로 확인할 수 있는 것은 harness 로 확인하고 목록에 넣지 않는다.
- 항목마다 띄우는 방법 (config, scene), 조작 순서, 통과의 기준 (무엇이 보이거나 바뀌면 통과인지) 을 적는다.
- 목록의 항목은 완료 보고에 다루지 않은 조건으로도 적는다 (CLAUDE.md 의 완료 판정 기준).
- computer use 는 쓰지 않는다. computer use 는 session 마다 사용자가 권한 요청을 승인해야 쓸 수 있다. 사용자는 작업 중에 권한 요청을 지켜보지 않는다. 그래서 권한 요청을 보내면 작업이 승인을 기다리며 멈추고, 끝내 승인받지 못한 것으로 처리된다.

## 예

```
python scripts/viewer.py start
python scripts/viewer.py do "ui hide" "state light" "light set elevation=20 azimuth=200" "pick grid 16x9"
python scripts/viewer.py do "settings set Engine Tuning: Ray Tracing/View=Validation: Depth Comparison" "frames 2" "validation capture" "validation fit"
python scripts/viewer.py shot overlay --max-width 640
python scripts/viewer.py run scripts/harness-scenarios/acceptance.txt
python scripts/viewer.py stop

python scripts/viewer.py snapshot save task-7      # 코드를 고치기 전
python scripts/viewer.py snapshot compare task-7   # 고친 뒤
```
