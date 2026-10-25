# Harness 로드맵

harness 는 agent 가 ModelViewer 를 build, 실행하고 명령으로 조작, capture, 판정하는 도구다. 쓰는 법은 `.claude/skills/modelviewer/SKILL.md`, CLI 는 `python scripts/viewer.py --help`, 명령의 protocol 은 `MiniEngine/Core/Automation/ControlChannel.h`, 종료 이유는 `MiniEngine/Core/Automation/UnattendedRun.h`, app flag 는 `MiniEngine/readme.md` 에 있다. 끝난 일은 코드와 위 문서가 진실이므로 여기에 다시 적지 않는다. "로드맵 작업 N" 은 `docs/reference-path-tracer-roadmap.md` 의 작업 N 이다.

이 문서에는 아직 하지 않은 일의 후보와, 그 일이 언제 왜 필요한지만 둔다. 어떻게 만들지 (고칠 파일, 명령의 형태, 완료 기준) 는 그 일을 시작할 때 코드를 보고 정한다. dcc4615 의 이 문서에는 미리 조사해 둔 구현 방법과 완료 기준이 있다. 시작할 때 출발점으로 참고하되 그대로 따르지 않는다.

## 원칙

- harness 는 미리 만들지 않는다. 로드맵의 작업을 하다가 정말 필요하다고 느낄 때, 그때 필요한 만큼만 만든다. 아래 후보의 때는 예상일 뿐 일정이 아니다.
- 판정은 SKILL.md "판정" 절의 원칙을 따른다. 새로 만드는 기능의 완료 기준도 text 와 수치로 정한다.

## 후보

### 입력 주입 (필요해질 때)

- 할 일: app 이 실제 입력 대신 명령으로 정한 mouse 와 keyboard 입력을 읽게 해서, camera 조작과 UI click 을 명령으로 한다.
- 근거
  - computer use 로는 camera 를 날릴 수 없다 (SKILL.md "computer use" 절). `pose` 는 camera 를 정하지만 EditorCamera 의 입력 경로를 지나지 않는다.
  - `pose` 는 velocity 를 만들지 않으므로, motion vector 를 쓰는 경로 (예: TAA) 는 입력 주입이 있어야 검사할 수 있다.
  - 입력을 frame 단위로 정할 수 있다. OS 입력은 frame 과 맞지 않아 같은 scenario 가 실행마다 다른 frame 수로 들어간다 (추측).

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

### UI 상태를 text 로 노출 (필요해질 때)

- 할 일: UI 가 바꾸는 상태 중 text 로 읽을 수 없는 것 (예: `SceneEditor` 가 고른 object 와 gizmo mode) 을 명령이나 `status` 로 노출한다.
- 근거: SKILL.md "판정" 절은 text 로 읽을 수 없는 상태를 바꾸는 조작의 결과를 판정하지 못한다고 정한다. 입력 주입이 UI 를 조작하게 되면 그 결과를 읽을 명령이 있어야 판정할 수 있다.

### 측정 (로드맵 작업 5, 6, 12 즈음)

- 할 일: Profiler 의 GPU time 과 CPU time 을 명령으로 읽는다. PT 의 export 를 `pt` 명령으로 한다 (로드맵 작업 6 에 넣을지는 그때 사용자가 정한다).
- 근거: 로드맵 작업 5, 12 가 PT 의 GPU time 을 재고, 작업 6 이 export 를 만든다.
- 만들지 않기로 한 것: pixel 값이나 영역 통계를 읽는 명령. scene capture 의 DDS 를 Python 으로 읽으면 된다.

### pixtool wrapper (필요해질 때)

- 할 일: `viewer.py` 가 pixtool 의 launch 와 GPU capture 를 감싼다.
- 근거: GPU capture, resource 저장, shader debugging 이 필요할 때 쓴다. RenderDoc 은 DXR 을 지원하지 않아 고르지 않았다.

## 보류한 결정

### MCP server

- harness 를 MCP server 로 감싸지 않는다.
  - CLI 로 기능이 다 된다.
  - MCP tool 의 출력은 25k token 에서 잘린다. image 가 모델에 그대로 넘어가는지는 확인하지 않았다.
  - 코드를 고칠 때마다 server 를 다시 띄우고 승인을 받아야 한다.
- 다시 볼 조건: shell 을 쓸 수 없는 client 에서 harness 를 써야 할 때, 또는 명령마다 Python 을 띄워 pipe 에 붙는 비용이 측정에 걸릴 때 (추측)
