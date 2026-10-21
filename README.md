# MiniEngine-DynamicGI

[DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) 의 MiniEngine 에 Dynamic Global Illumination 을 구현하는 프로젝트.

## 준비

- Visual Studio 2022 (17.14 에서 확인), Desktop development with C++ workload, vcpkg 구성 요소
- Windows SDK
- inline raytracing (DXR Tier 1.1, Shader Model 6.5) 을 지원하는 GPU. WARP 는 지원하지 않는다.

## 빌드와 실행

1. asset 을 받는다. ModelViewer 가 쓰는 Sponza 와 IBL texture 는 용량 때문에 repo 에 두지 않는다.

   ```powershell
   .\scripts\fetch-assets.ps1
   ```

2. `MiniEngine/MiniEngine.slnx` 를 열고 ModelViewer 를 build 후 실행한다. DirectXTex, DirectXMesh 는 첫 build 때 vcpkg 가 받는다.

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
- MIT 처럼 허용하는 license 의 코드는 가져다 쓸 수 있다. 가져온 코드에는 원래의 copyright 와 license 문구를 남긴다.
- license 가 없거나 재배포를 막는 코드는 사용하지 않는다. 코드가 구현한 idea만 참고한다.

## Rendering 기준

asset 을 해석하고 그리는 방식은 glTF 2.0 spec 을 기준으로 한다. raster, ray tracing, GI 가 같은 기준을 따라야 서로의 결과나 외부 renderer 의 결과를 비교할 때 차이가 기준에서 오지 않는다.

- 기준 spec 은 `references/glTF/specification/2.0/Specification.adoc` 이다. 어느 판인지는 [references/manifest.json](references/manifest.json) 의 `glTF` commit 이 정한다.
- spec 과 다르게 구현한 곳에는 `glTF spec과 다르다` 로 시작하는 주석으로 무엇이 다른지와 그 이유를 적는다. 이 문구로 검색하면 spec 과 다른 곳을 모두 찾을 수 있다.
- upstream MiniEngine 의 raster 는 아직 이 기준에 맞추지 않았으므로, raster 에서 spec 과 다른 곳에는 이 주석이 없다.

## 코드 규칙

- 이 프로젝트에서 작성하거나 수정한 코드의 주석은 한국어로 쓰고, 기술 용어는 영어로 쓴다. upstream 코드의 주석은 그대로 둔다.
- source 파일은 UTF-8 로 저장한다. editor 는 [.editorconfig](.editorconfig), MSVC 는 `/utf-8` ([Build.props](MiniEngine/PropertySheets/Build.props)) 로 이를 따른다.

### 폴더 구조

- source 파일은 기능별 폴더에 둔다. C++ 파일과 그 파일이 쓰는 shader 는 같은 폴더에 둔다. 여러 기능이 공유하는 `.hlsli` 는 `MiniEngine/Core/Shaders/` 에 둔다.
- `#include` 는 C++ 와 shader 모두 `MiniEngine/` 기준 경로로 쓰고, 같은 폴더의 파일이나 pch 도 예외 없이 project 이름부터 쓴다 (예: `#include "Core/pch.h"`, `#include "Model/Renderer/Common.hlsli"`). build 가 만드는 shader header 는 `MiniEngine/Build/<Platform>/<Configuration>/` 기준 경로로 쓴다 (예: `#include "Output/Core/CompiledShaders/ScreenQuadPresentVS.h"`).
- Visual Studio filter (`.vcxproj.filters`) 는 쓰지 않는다. Solution Explorer 에서 Show All Files 를 켜고 폴더 구조 그대로 본다.

## License

이 프로젝트의 작업은 MIT License 를 따른다([LICENSE](LICENSE)). upstream MiniEngine 코드는 Microsoft 의 MIT License 를 따른다([MiniEngine/License.txt](MiniEngine/License.txt)). Sponza asset 의 출처는 받은 뒤 `MiniEngine/ModelViewer/Sponza/pbr/copyright.txt` 에 있다.
