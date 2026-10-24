# Harness 로드맵

harness 는 agent 가 ModelViewer 를 build, 실행하고 명령으로 조작, capture, 판정하는 도구다. 쓰는 법은 `.claude/skills/modelviewer/SKILL.md`, CLI 는 `python scripts/viewer.py --help`, 명령의 protocol 은 `MiniEngine/Core/Automation/ControlChannel.h`, 종료 이유는 `MiniEngine/Core/Automation/UnattendedRun.h`, app flag 는 `MiniEngine/readme.md` 에 있다. 이 문서에는 아직 하지 않은 일과, 그 일을 할 때 필요한 결정과 근거만 둔다. 끝난 일은 코드와 위 문서가 진실이므로 여기에 다시 적지 않는다. "로드맵 작업 N" 은 `docs/reference-path-tracer-roadmap.md` 의 작업 N 이다.

## 전제

- 판정은 SKILL.md "판정" 절의 원칙을 따른다. 아래 작업의 완료 기준도 text 와 수치로 정한다.
- 명령은 frame 을 시작하기 전에 실행된다 (`GameCore::UpdateApplication` 의 주석). 아래 입력 주입은 이 순서에 기댄다.
- 결정성의 한계 (TAA 와 adaptive exposure, 명령 사이의 frame 수) 는 SKILL.md "결정성" 절에 있다. 이 문서의 작업은 그 한계를 바꾸지 않는다.

## 작업

순서는 로드맵의 작업에 필요한 때를 따른다. 입력 주입, 진단, UI 상태 노출은 로드맵 작업 3 (PT pass 와 누적) 전에 한다. 작업 3 이 PT panel 을 만들고, 그 UI 흐름과 camera 이동에 따른 reset 을 확인해야 하기 때문이다.

### 입력 주입 (로드맵 작업 3 전)

- 할 일
  - `-input virtual` flag 를 더한다. `KbmUpdate` (`Core/App/Input.cpp`) 가 DirectInput 대신 virtual 상태를 읽고 foreground 검사를 건너뛴다. camera 가 cursor 를 잡는 동안의 `GetCursorPos`, `SetCursorPos` (같은 파일) 도 virtual 로 바꾼다. 그러면 UI 와 camera 가 입력을 나누는 지금의 규칙이 그대로 돈다.
  - ImGui 에는 `ImGuiLayer::BeginFrame` 의 `ImGui_ImplWin32_NewFrame` 뒤에 `io.Add*Event` 로 넣는다. 실제 입력 message 와 focus message 는 `ImGuiLayer::HandleWindowMessage` 에서 버린다. F1 은 그 함수가 `WM_KEYDOWN` 으로 처리하므로 virtual F1 은 따로 넘긴다.
  - 명령: `mouse move|down|up|click|drag|wheel`, `key down|up|press <name> [frames=n]`, `type <text>`, `input release`.
  - `start --input virtual|real`. virtual 은 실제 입력을 버리므로 computer use 로 실제 입력 경로를 볼 때는 real 로 띄우거나 `input release` 를 보낸다. 기본값은 이 작업에서 정한다.
  - `shot --from-shot`: 마지막 shot 의 view 좌표를 client 좌표로 바꿔 mouse 명령에 준다. display 는 client 와 크기가 같아 view 축척과 crop 만 되돌리면 된다. scene 과 overlay 는 scene 영역의 원점도 필요한데 `status` 는 scene 의 크기만 돌려주므로 원점을 더한다.
- 근거
  - computer use 로는 camera 를 날릴 수 없다 (이유는 SKILL.md "computer use" 절). `pose` 는 camera 를 정하지만 EditorCamera 의 입력 경로를 지나지 않는다.
  - `pose` 는 velocity 를 만들지 않는다. `EditorCamera::SetPose` 와 같은 frame 의 `EditorCamera::Update` 가 둘 다 `Camera::Update` 를 불러, 그리는 시점에는 앞 frame 의 view-projection 이 지금과 같아진다. 그래서 velocity (motion vector) 를 쓰는 경로 (예: `TemporalBlendCS` 의 Speed Limit) 는 입력 주입이 있어야 검사할 수 있다.
  - frame 단위로 입력을 정할 수 있다 (예: W 를 30 frame). OS 입력은 frame 과 맞지 않아 같은 scenario 가 실행마다 다른 frame 수로 들어간다 (추측).
  - 명령이 frame 을 시작하기 전에 실행되므로, 명령이 정한 virtual 상태를 같은 frame 의 `GameInput::Update` 와 ImGui 가 읽는다.
  - Win32 message 를 합성하지 않는 이유: imgui_impl_win32 backend 가 modifier 를 `GetKeyState` 로 읽고, 창이 foreground 면 `GetCursorPos` 로 mouse 위치를 덮는다. event 를 `ImGui_ImplWin32_NewFrame` 뒤에 넣으면 그 값을 이긴다 (추측, 아래 완료 기준으로 확인).
- 한계: virtual 입력은 Win32 message, DirectInput, `SetCursorPos` 경로를 건너뛴다. 그 경로는 computer use 로 본다.
- 수정: `Core/Automation/VirtualInput.h, .cpp` (새), `Core/App/Input.cpp`, `Core/App/GameInput.h`, `Core/UI/ImGuiLayer.cpp`, `Core/Automation/EngineCommands.cpp`, `Core/Core.vcxproj`, `scripts/viewer.py`, `MiniEngine/readme.md`, `.claude/skills/modelviewer/SKILL.md`
- 완료 기준 (selftest 에 더한다)
  - W 와 오른쪽 button 을 60 frame 누르면 `pose` 가 `Move Speed × 60 × dt` 만큼 앞으로 가고, S 로 되돌아온다.
  - cursor 가 panel 위에 있으면 오른쪽 button 을 눌러도 `pose` 가 그대로다.
  - View menu 를 click 하면 `settings get Panels` 의 값이 바뀐다. virtual F1 뒤에 `status` 의 ui 가 hidden 이다.
  - virtual input 으로 camera 를 움직이는 동안 velocity 가 0 이 아니고, velocity 를 쓰는 경로의 결과가 정지한 camera 와 다르다. 판정 수단은 이 작업에서 정한다. 후보는 velocity buffer 를 capture target 으로 더해 0 이 아닌 pixel 을 세는 것과, 움직이는 동안 TAA 의 Speed Limit 만 바꾼 두 scene capture 가 다르고 정지한 camera 에서는 같은지 보는 것이다. TAA 를 켠 capture 는 지난 frame 수에 따라 달라지므로 (SKILL.md "결정성" 절) 뒤의 방법은 두 run 의 명령과 frame 수를 맞춘다.

### 진단 (로드맵 작업 3 전)

- 할 일
  - D3D12 message 를 `ID3D12InfoQueue1` 의 callback 으로 받아 log 에 쓴다. 지금의 info queue 설정은 `Graphics::Initialize` (`Core/Graphics/GraphicsCore.cpp`) 에 있다.
  - DRED 를 켠다.
  - `Display::Present` 가 버리는 `Present` 의 HRESULT 로 device removed 와 reset 을 잡는다. 그때 `GetDeviceRemovedReason` 과 DRED 를 log 에 쓰고 끝낸다. 새 종료 이유는 `UnattendedRun.h` 의 목록에 더한다.
  - minidump 를 run 폴더에 남긴다. crash 는 `UnattendedRun` 의 filter 가 process 안에서, hang 은 viewer.py 가 kill 하기 전에 process 밖에서 쓴다. `stop --dump` 를 더한다.
  - `stop` 의 요약에 D3D12 error 수를 넣는다.
  - `fault device_removed` (`ID3D12Device5::RemoveDevice`) 를 더한다.
- 근거
  - 지금은 D3D12 debug layer 의 message 가 debugger 출력으로만 나가 harness 가 볼 수 없다. SKILL.md "쓰는 범위" 절이 이것을 사용자에게 부탁하는 이유다.
  - hang 과 crash 의 원인을 log 와 종료 이유만으로 찾기 어렵다. dump 가 있으면 사용자에게 재현을 부탁하지 않고 그 자리를 볼 수 있다.
- 수정: `Core/Graphics/GraphicsCore.cpp`, `Core/Display/Display.cpp`, `Core/Automation/UnattendedRun.h, .cpp`, `Core/Automation/EngineCommands.cpp`, `scripts/viewer.py`, `.claude/skills/modelviewer/SKILL.md`
- 완료 기준 (selftest 에 더한다)
  - `fault device_removed` 뒤에 log 에 종료 이유와 DRED 가 남는다.
  - 일부러 D3D12 error 를 낸 호출이 log 에 잡히고 `stop` 이 그 수를 센다.
  - `fault hang` 뒤 `stop` 이 dump 를 남기고 kill 한다.
- 이 작업 때 함께 볼 것
  - `PostEffects::ExtractLuma` (`Core/Effects/PostEffects/PostEffects.cpp:320`) 가 `g_SceneColorBuffer` 를 transition 없이 SRV 로 읽는다. Bloom 을 끄고 adaptive exposure 를 켜서 `ProcessHDR` 이 `ExtractLuma` 를 부를 때다. harness 이전부터 있던 문제다. D3D12 message 가 log 에 남게 되면 validation error 를 확인하고 따로 fix commit 으로 고친다. 사용자가 이 시점으로 정했다.
  - 아직 실행으로 확인하지 않은 경로: MAX_PATH 를 넘는 `-settings_dir`, `__fastfail`, stack overflow, debugger 가 붙은 경우, Release build, 두 viewer.py 호출 사이에 app 이 끝난 경우 (exit code 를 알 수 없다고 보고한다), 두 thread 의 동시 crash. dump 와 D3D12 message 가 생기면 확인할 수단이 늘어나므로 이때 돌려 본다.

### UI 상태를 text 로 노출 (로드맵 작업 3 전)

- 할 일: UI 가 바꾸는 상태 중 text 로 읽을 수 없는 것을 조사해 명령이나 `status` 로 노출한다. 지금 아는 것은 `SceneEditor` 가 고른 object 와 gizmo mode (translate, rotate), sun icon 의 위치 (`ModelViewer.cpp` 의 `Sun`) 다.
- 근거: SKILL.md "판정" 절은 text 로 읽을 수 없는 상태를 바꾸는 조작의 결과를 판정하지 못한다고 정한다. 입력 주입이 UI 를 조작하게 되면 그 결과를 읽을 명령이 있어야 판정할 수 있다.
- 완료 기준: selftest 와 로드맵의 확인에서 쓰는 UI 조작마다, 조작 전과 뒤의 상태를 text 로 비교할 수 있다. 그 뒤 SKILL.md "판정" 절의 예에서 해당 상태를 뺀다.

### 측정 (로드맵 작업 5, 6, 12 즈음)

- `timing <path>`: Profiler 의 GPU time 과 CPU time 을 path 로 읽는다 (`Core/Profiling/EngineProfiling`). Release 에서는 `ScopedTimer` 가 no-op 이라 error 를 돌려준다. capture 한 frame 은 GPU 를 기다리므로 (`FrameCapture.h`) 그 frame 은 재지 않는다. 완료 기준: Profile 에서 값을, Release 에서 error 를 돌려준다 (selftest).
- DDS reader 를 `scripts/imagefiles.py` 로 옮겨 로드맵의 `scripts/compare-images.py` 와 같이 쓴다. 로드맵 작업 6 이 `compare-images.py` 를 만들 때 한다. 두 script 가 같은 파일을 다르게 읽지 않게 하려는 것이다.
- PT 가 자기 명령 (`pt status|reset|wait samples=|export dir=`) 을 등록한다. 로드맵 작업 3, 6 에 이 항목을 넣을지는 그때 사용자가 정한다.
- 검토만 한 것: pixel 값이나 영역 통계를 읽는 명령. 지금은 scene capture 의 DDS 를 Python 으로 읽으면 되므로 만들지 않는다.

### pixtool wrapper (필요해질 때)

- `viewer.py pix` 로 pixtool 의 launch 와 GPU capture 를 감싼다. GPU capture, resource 저장, shader debugging 이 필요할 때 만든다.
- PIX 2603.25 가 설치되어 있다. WinPixEventRuntime package 의 `pix3_win.h` 에 `PIXLoadLatestWinPixGpuCapturerLibrary` 와 `PIXGpuCaptureNextFrames` 가 있어 app 이 명령으로 다음 frame 을 capture 하게 할 수도 있다.
- RenderDoc 은 DXR 을 지원하지 않아 고르지 않았다.

## 보류한 결정

### MCP server

- harness 를 MCP server 로 감싸지 않는다.
  - CLI 로 기능이 다 된다.
  - MCP tool 의 출력은 25k token 에서 잘린다. image 가 모델에 그대로 넘어가는지는 확인하지 않았다.
  - 코드를 고칠 때마다 server 를 다시 띄우고 승인을 받아야 한다.
- 다시 볼 조건: shell 을 쓸 수 없는 client 에서 harness 를 써야 할 때, 또는 명령마다 Python 을 띄워 pipe 에 붙는 비용이 측정에 걸릴 때 (추측). 그때는 viewer.py 를 부르는 얇은 wrapper 로 만들어 `.mcp.json` 에 둔다. Claude desktop app 도 같은 설정을 읽는다.
