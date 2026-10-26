# Harness 로드맵

harness 는 agent 가 ModelViewer 를 build, 실행하고 명령으로 조작, capture, 판정하는 도구다. 쓰는 법은 `.claude/skills/modelviewer/SKILL.md`, CLI 는 `python scripts/viewer.py --help`, 명령의 protocol 은 `MiniEngine/Core/Automation/ControlChannel.h`, 종료 이유는 `MiniEngine/Core/Automation/UnattendedRun.h`, app flag 는 `MiniEngine/readme.md` 에 있다. 끝난 일은 코드와 위 문서가 진실이므로 여기에 다시 적지 않는다. "로드맵 작업 N" 은 `docs/reference-path-tracer-roadmap.md` 의 작업 N 이다.

이 문서에는 아직 하지 않은 일의 후보와, 그 일이 언제 왜 필요한지만 둔다. 어떻게 만들지 (고칠 파일, 명령의 형태, 완료 기준) 는 그 일을 시작할 때 코드를 보고 정한다. dcc4615 의 이 문서에는 미리 조사해 둔 구현 방법과 완료 기준이 있다. 시작할 때 출발점으로 참고하되 그대로 따르지 않는다.

## 원칙

- harness 는 미리 만들지 않는다. 로드맵의 작업을 하다가 정말 필요하다고 느낄 때, 그때 필요한 만큼만 만든다. 아래 후보의 때는 예상일 뿐 일정이 아니다.
- 판정은 SKILL.md "판정" 절의 원칙을 따른다. 새로 만드는 기능의 완료 기준도 text 와 수치로 정한다.

## 후보

### 여러 frame 에 걸친 camera 움직임 (필요해질 때)

- 할 일: camera 가 여러 frame 에 걸쳐 움직이게 해서 motion vector 를 쓰는 경로 (예: TAA) 를 검사한다. 입력 주입으로 할지 pose 를 frame 마다 옮기는 명령으로 할지는 그때 정한다. 조작 수단으로서의 입력 주입은 만들지 않는다 (보류한 결정).
- 근거: `pose` 는 velocity 를 만들지 않으므로, motion vector 를 쓰는 경로는 지금 검사할 수 없다. `snapshot` 도 camera 가 멈춘 shot 만 찍는다.

### 기본 효과를 켠 불변식 (필요해질 때)

- 할 일: app 의 기본 효과 (TAA, motion blur, adaptive exposure) 를 켠 채로, 작업과 관계없이 늘 성립해야 하는 불변식을 모아 rendering 을 바꾸는 작업마다 돌린다. 예를 들어 camera 가 멈추면 motion blur 가 화면을 바꾸지 않는다.
- 근거: 로드맵 작업 C 의 첫 판정은 baseline 에서만 해서, SSAO pass 와 함께 빠진 linear depth 가 motion blur 를 번지게 한 것을 놓쳤다 (`scripts/check-raster-lighting.py` 의 [13]). `snapshot compare` 가 같은 code 의 차이보다 큰 변화를 표시하지만, 그 변화가 bug 인지는 agent 가 sheet 를 보고 판단한다. 불변식은 bit 단위로 판정한다.
- 때: `snapshot compare` 가 놓친 회귀가 다시 나오거나, 같은 불변식을 여러 작업의 check script 가 따로 갖게 될 때

### Validation view 를 끈 뒤의 비결정성 원인 (필요해질 때)

- 할 일: Validation view 를 끈 직후의 scene 이 몇 pixel 다른 원인 (SKILL.md "결정성" 절) 을 찾는다.
- 근거: harness 는 끈 뒤 기다려서 피하고 있을 뿐이다. 원인이 GPU 의 동기화 문제라면 다른 pass 의 결과도 timing 에 따라 바뀔 수 있다 (추측). 기다림으로 덮지 못하는 비교가 생기거나, 진단 후보의 GPU capture 를 쓸 수 있게 되면 본다.

### 진단 (필요해질 때)

- 할 일: device removed, crash, hang 이 났을 때 원인을 볼 자료 (DRED, minidump, DXGI message) 를 log 와 run 폴더에 남긴다. GBV message 가 log 에 닿는지 보이는 양성 대조군을 더한다.
- 근거
  - hang 과 crash 의 원인을 log 와 종료 이유만으로 찾기 어렵다. dump 가 있으면 사용자에게 재현을 부탁하지 않고 그 자리를 볼 수 있다.
  - GBV message 가 log 에 닿는다는 것을 아직 보이지 않았다. 그래서 GBV 에서 message 가 0 이라는 결과는 그 경로에 문제가 없다는 근거로 약하다.
  - DXGI message 는 debugger 출력으로만 나가, SKILL.md "쓰는 범위" 절이 사용자에게 확인을 부탁한다.
- 함께 할 일: 아직 실행으로 확인하지 않은 종료 경로 (`__fastfail`, stack overflow, debugger 가 붙은 경우 등) 를 dump 로 확인한다.

### 진단 출력 (진단과 함께)

- 할 일: `stop` 의 D3D12 요약이 남은 resource 의 이름을 보이고 run 폴더에 파일로도 남는다. `do` 와 `run` 이 명령마다 늘어난 message 를 보인다.
- 근거: 573dded 의 log 만으로 Lighting resource 해제 누락 (e8a2352) 을 진단한 agent 가 보고한 불편이다. 어느 resource 가 남았는지 보려면 app.log 를 열어야 했고, 요약을 증거로 남기려면 옮겨 적어야 했고, 어느 명령이 message 를 냈는지 좁히려면 명령 사이에 `status` 를 끼워야 했다.
- 함께 볼 것: Debug 의 CRT message 가 `\n` 으로 끝나지 않으면 `exit reason=` 줄이 그 뒤에 붙어 viewer.py 가 찾지 못할 수 있다 (`OnCrtReport`, 추측, 확인하지 않음).

### 측정 (필요해질 때)

- 할 일: Profiler 의 CPU time 이 필요해지면 `profile` 명령에 더한다.
- 만들지 않기로 한 것: pixel 값이나 영역 통계를 읽는 명령. `capture` 의 DDS 를 Python 으로 읽으면 된다.

### pixtool wrapper (필요해질 때)

- 할 일: `viewer.py` 가 pixtool 의 launch 와 GPU capture 를 감싼다.
- 근거: GPU capture, resource 저장, shader debugging 이 필요할 때 쓴다. RenderDoc 은 DXR 을 지원하지 않아 고르지 않았다.

## 보류한 결정

### 사람의 입력을 흉내 내는 조작

- harness 는 mouse 와 keyboard 입력이나 UI click 을 흉내 내어 app 을 조작하지 않는다. 판정은 `state` 로 하고, UI 와 gizmo 가 바꾸는 상태는 같은 상태를 바꾸는 의미 단위 명령 (예: `sun set elevation= azimuth=`, `editor mode`) 으로 바꾼다. screenshot 은 참고로만 본다.
  - agent 가 sun 의 translate gizmo 를 rotate gizmo 로 잘못 보고, translate gizmo 를 옮긴 뒤 sun 이 돌지 않은 것이 이상하다고 보고한 일이 있었다. image 로 UI 의 상태를 판단하는 것은 믿을 수 없고, 입력을 흉내 내는 조작은 click 할 곳과 결과를 그 판단에 기댄다.
- 다시 볼 조건: WASD 나 gizmo drag 같은 입력 처리 코드 자체를 harness 로 검사해야 할 때. 그때까지 그 확인은 사용자가 한다.

### MCP server

- harness 를 MCP server 로 감싸지 않는다.
  - CLI 로 기능이 다 된다.
  - MCP tool 의 출력은 25k token 에서 잘린다. image 가 모델에 그대로 넘어가는지는 확인하지 않았다.
  - 코드를 고칠 때마다 server 를 다시 띄우고 승인을 받아야 한다.
- 다시 볼 조건: shell 을 쓸 수 없는 client 에서 harness 를 써야 할 때, 또는 명령마다 Python 을 띄워 pipe 에 붙는 비용이 측정에 걸릴 때 (추측)
