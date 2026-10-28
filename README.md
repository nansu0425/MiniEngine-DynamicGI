# MiniEngine-DynamicGI

[DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) 의 MiniEngine 에 Dynamic Global Illumination 을 구현하는 프로젝트.

## 준비

- Visual Studio 2022 (17.14 에서 확인), Desktop development with C++ workload, vcpkg 구성 요소
- Windows SDK
- inline raytracing (DXR Tier 1.1, Shader Model 6.5) 을 지원하는 GPU. WARP 는 지원하지 않는다.
- Python 3.12 와 [scripts/requirements.txt](scripts/requirements.txt) 의 package (`pip install -r scripts/requirements.txt`). app 을 build 하고 실행해 확인하는 `scripts/viewer.py` 와 `scripts/` 의 확인 script 가 쓴다.
- VS Code 로 build 하고 실행하려면 C/C++ extension (`ms-vscode.cpptools`) 1.32.2 이상. Run Without Debugging 을 이 version 부터 지원한다. VS Code 의 build 는 `scripts/viewer.py` 로 하므로 Python 도 필요하다.

## 빌드와 실행

1. asset 을 받는다. ModelViewer 가 쓰는 Sponza (KhronosGroup glTF-Sample-Assets) 는 용량 때문에 repo 에 두지 않는다.

   ```powershell
   .\scripts\fetch-assets.ps1
   ```

2. `MiniEngine/MiniEngine.slnx` 를 열고 ModelViewer 를 build 후 실행한다. DirectXTex, DirectXMesh 는 첫 build 때 vcpkg 가 받는다.

   VS Code 에서는 repo root 를 열고 Visual Studio 와 같은 단축키를 쓴다.

   - Ctrl+Shift+B: Debug 를 build 한다. Profile 과 Release 는 Terminal > Run Task 의 `build Profile`, `build Release` 로 build 한다.
   - Ctrl+F5: Run and Debug 에서 고른 configuration 을 build 한 뒤 실행한다.
   - F5: Ctrl+F5 와 같고, debugger 를 붙여 실행한다.

조작법은 [MiniEngine/readme.md](MiniEngine/readme.md) 에 있다.

## Reference

구현할 때 참고하는 외부 자료는 `references/` 아래에 받는다. git 은 [references/manifest.json](references/manifest.json) 만 tracking 하고, 받은 자료는 tracking 하지 않는다.

```powershell
.\scripts\fetch-references.ps1
```

- git repo 는 `manifest.json` 에 `name`, `url`, `commit` 을 적고 script 를 다시 실행한다. commit 을 고정해서 어느 버전을 참고했는지 git history 에 남긴다.
- repo 의 일부만 필요하면 `sparse` 에 [non-cone sparse-checkout pattern](https://git-scm.com/docs/git-sparse-checkout#_non_cone_problems) 목록을 적는다. 이 pattern 에 맞는 파일만 받는다. `sparse` 를 지우면 다음 실행 때 전체를 받는다.
- submodule 은 `submodules` 에 경로를 적은 것만 받는다. `sparse` 는 submodule 에 적용되지 않는다.
- 받은 repo 안에서 수정한 내용이 있으면 script 는 그 repo 를 건너뛴다.
- 논문 같은 단일 파일은 `name`, `url`, `sha256` 을 적는다. 받은 파일의 sha256 이 다르면 script 가 실패한다.
- manifest 에서 뺀 항목은 script 가 지우지 않는다. `references/` 아래의 해당 폴더나 파일을 직접 지운다.
- `git clean -fdx` 는 `references/` 도 지운다. 지워지면 script 로 다시 받는다.

### 참고 범위

이 repo 는 MIT 로 공개한다. 그래서 `references/` 에는 이 repo 에서 자유롭게 참고할 수 있는 자료만 둔다.

- 논문은 알고리즘과 수식을 참고해 구현한다. 본문과 그림은 repo 에 옮기지 않는다.
- MIT 처럼 허용하는 license 의 코드는 가져다 쓸 수 있다. 가져온 코드는 다음처럼 표시한다.
  - 가져온 함수나 class 의 주석에 출처 project 와 파일 경로, 원래 이름을 적고, license 는 `THIRD_PARTY_NOTICES.md` 에 있다고 적는다. 고친 점이 있으면 이어서 적는다. 예: `// Capsaicin의 src/core/src/geometry/geometry.hlsl에서 offsetPosition을 가져왔다. license는 THIRD_PARTY_NOTICES.md에 있다.` `THIRD_PARTY_NOTICES.md` 로 검색하면 가져온 코드를 모두 찾을 수 있다.
  - copyright 와 license 문구의 전문은 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 에 출처 project 마다 한 번만 둔다. MIT 는 문구를 copy 에 포함하라고만 하고 위치는 정하지 않는다. 새 project 에서 코드를 가져오면 `THIRD_PARTY_NOTICES.md` 에 새 project 의 항목을 더한다.
  - 파일 상단에는 license 전문을 붙이지 않는다. 파일 전체가 다른 project 에서 가져온 코드처럼 보이기 때문이다. 파일을 통째로 가져왔을 때만 원래 파일의 header 를 그대로 둔다.
- license 가 없거나 재배포를 막는 코드는 사용하지 않는다. 코드가 구현한 idea만 참고한다.

## Rendering 기준

asset 을 해석하고 그리는 방식은 glTF 2.0 spec 을 기준으로 한다. raster, ray tracing, GI 가 같은 기준을 따라야 서로의 결과나 외부 renderer 의 결과를 비교할 때 차이가 기준에서 오지 않는다.

- 기준 spec 은 `references/glTF/specification/2.0/Specification.adoc` 이다. 어느 판인지는 [references/manifest.json](references/manifest.json) 의 `glTF` commit 이 정한다.
- spec 과 다르게 구현한 곳에는 `glTF spec과 다르다` 로 시작하는 주석으로 무엇이 다른지와 그 이유를 적는다. 이 문구로 검색하면 spec 과 다른 곳을 모두 찾을 수 있다.
- raster 의 directional light 는 PT 와 같은 glTF BRDF (`MiniEngine/Core/Shaders/GltfBrdf.hlsli`) 로 반사한다. 그 밖의 upstream MiniEngine 의 raster 는 아직 이 기준에 맞추지 않았으므로, raster 에서 spec 과 다른 곳에는 이 주석이 없다.
- 작업이 spec 에 맞춰야 하는 곳에서 기존 engine 코드가 spec 과 다르면, 그 작업에서 engine 코드도 spec 에 맞게 고친다.
- Reference Path Tracer 는 다른 기능의 정답이므로 spec 의 MUST 와 SHOULD 를 모두 따른다. PT 가 쓰는 engine 코드도 이 기준으로 고친다.

### camera 밖에서 본 doubleSided

spec 의 doubleSided 는 camera 에서 그릴 때의 결과만 정하고, shadow 나 GI 처럼 camera 가 아닌 곳에서 본 결과는 정하지 않는다. 이 repo 는 doubleSided 를 한 시점에서 무엇이 보이는지의 규칙으로 읽고, 무엇이 보이는지나 가려졌는지를 구하는 query 의 시점에 같은 규칙을 적용한다.

- ray 를 쏘는 query 의 시점은 ray 의 출발점이다. 출발점에서 본 single-sided material 의 뒷면은 없는 것으로 친다. camera ray, reflection ray, path tracing 의 path ray 가 모두 그렇다.
- 두 점 사이의 visibility 는 shading 하는 점에서 상대 점이 보이는지이고, ray 는 shading 하는 점에서 쏜다. single-sided 면이 사이에 있으면 반대 방향으로 쏜 결과와 다를 수 있다.
- shading 하는 점이 놓인 면이 geometric normal 로 상대 점을 등지면 가려진 것으로 친다. 그 점에서 출발한 ray 는 바로 그 면을 앞에서 지나기 때문이다. normal map 을 적용한 shading normal 이 상대 점을 향해도 같다.
- closest hit 와 visibility 가 같은 규칙을 따르므로 같은 ray 에서 둘이 보는 면이 같다. path tracing 의 MIS 처럼 두 query 의 결과를 섞는 곳이 이것에 기댄다.
- raster 의 shadow map 은 light 에서 보므로, shading 하는 점에서 본 결과와 같도록 single-sided material 의 앞면을 cull 한다.
- 뒷면에 맞았는지를 알려는 query 는 cull 하지 않는다. DDGI probe 가 벽 안에 있는지 보는 ray 와, debug view 가 culling 을 끄고 보는 view 가 그렇다.
- 모든 ray 를 양면으로 두고 camera 에서만 cull 하는 방식은 고르지 않았다. ray 가 single-sided 의 뒷면에 맞으면 그 면을 shading 해야 하는데, spec 은 그 뒷면이 어떻게 보이는지 정하지 않는다.

## 코드 규칙

- 이 프로젝트에서 작성하거나 수정한 코드의 주석은 한국어로 쓰고, 기술 용어는 영어로 쓴다. upstream 코드의 주석은 그대로 둔다.
- source 파일은 UTF-8 로 저장한다. editor 는 [.editorconfig](.editorconfig), MSVC 는 `/utf-8` ([Build.props](MiniEngine/PropertySheets/Build.props)) 로 이를 따른다.

### 폴더 구조

- source 파일은 기능별 폴더에 둔다. C++ 파일과 그 파일이 쓰는 shader 는 같은 폴더에 둔다. 여러 기능이 공유하는 `.hlsli` 는 `MiniEngine/Core/Shaders/` 에 둔다.
- `#include` 는 C++ 와 shader 모두 `MiniEngine/` 기준 경로로 쓰고, 같은 폴더의 파일이나 pch 도 예외 없이 project 이름부터 쓴다 (예: `#include "Core/pch.h"`, `#include "Model/Renderer/Common.hlsli"`). build 가 만드는 shader header 는 `MiniEngine/Build/<Platform>/<Configuration>/` 기준 경로로 쓴다 (예: `#include "Output/Core/CompiledShaders/ScreenQuadPresentVS.h"`).
- Visual Studio filter (`.vcxproj.filters`) 는 쓰지 않는다. Solution Explorer 에서 Show All Files 를 켜고 폴더 구조 그대로 본다.

## License

이 프로젝트의 작업은 MIT License 를 따른다([LICENSE](LICENSE)). upstream MiniEngine 코드는 Microsoft 의 MIT License 를 따른다([MiniEngine/License.txt](MiniEngine/License.txt)). 다른 project 에서 가져온 코드의 copyright 와 license 는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 에 있다. Sponza asset 은 Cryengine Limited License Agreement 를 따르고, 그 license 표시는 받은 뒤 `MiniEngine/ModelViewer/glTF-Sample-Assets/Models/Sponza/LICENSE.md` 에 있다.
