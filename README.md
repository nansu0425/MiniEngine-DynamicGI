# MiniEngine-DynamicGI

[DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) 의 MiniEngine 에 Dynamic Global Illumination 을 구현하는 프로젝트.

## 준비

- Visual Studio 2022 (17.14 에서 확인), Desktop development with C++ workload, vcpkg 구성 요소
- Windows SDK
- inline raytracing (DXR Tier 1.1, Shader Model 6.5) 을 지원하는 GPU. WARP 는 지원하지 않는다.
- Python 3.12 와 [scripts/requirements.txt](scripts/requirements.txt) 의 package (`pip install -r scripts/requirements.txt`). app 을 build 하고 실행해 확인하는 `scripts/viewer.py` 와 `scripts/` 의 확인 script 가 쓴다.
- VS Code 로 build 하고 실행하려면 C/C++ extension (`ms-vscode.cpptools`) 1.32.2 이상. Run Without Debugging 을 이 version 부터 지원한다. VS Code 의 build 는 `scripts/viewer.py` 로 하므로 Python 도 필요하다.

## 빌드와 실행

1. asset 을 받는다. ModelViewer 가 쓰는 Sponza (KhronosGroup glTF-Sample-Assets) 는 용량 때문에 repo 에 두지 않는다. Cornell box (Benedikt Bitterli 의 Mitsuba 3 scene) 도 받아 `scripts/cornellbox.py` 로 glTF 로 바꾼다. scene 은 `MiniEngine/ModelViewer/Scenes/` 의 scene file 로 연다.

   ```powershell
   .\scripts\fetch-assets.ps1
   ```

2. `MiniEngine/MiniEngine.slnx` 를 열고 ModelViewer 를 build 후 실행한다. DirectXTex, DirectXMesh 는 첫 build 때 vcpkg 가 받는다.

   VS Code 에서는 repo root 를 열고 Visual Studio 와 같은 단축키를 쓴다.

   - Ctrl+Shift+B: Debug 를 build 한다. Profile 과 Release 는 Terminal > Run Task 의 `build Profile`, `build Release` 로 build 한다.
   - Ctrl+F5: Run and Debug 에서 고른 configuration 을 build 한 뒤 실행한다.
   - F5: Ctrl+F5 와 같고, debugger 를 붙여 실행한다.

조작법은 [MiniEngine/readme.md](MiniEngine/readme.md) 에 있다.

## 프로젝트 정책

이 repo 에서 작업할 때 따르는 정책은 [docs/policy.md](docs/policy.md) 에 있다.

## License

이 프로젝트의 작업은 MIT License 를 따른다([LICENSE](LICENSE)). upstream MiniEngine 코드는 Microsoft 의 MIT License 를 따른다([MiniEngine/License.txt](MiniEngine/License.txt)). 다른 project 에서 가져온 코드의 copyright 와 license 는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 에 있다. Sponza asset 은 Cryengine Limited License Agreement 를 따르고, 그 license 표시는 받은 뒤 `MiniEngine/ModelViewer/glTF-Sample-Assets/Models/Sponza/LICENSE.md` 에 있다. Cornell box asset 은 CC0 1.0 이고, 그 license 표시는 `scripts/fetch-assets.ps1` 이 받아 푼 zip 의 `cornell-box/LICENSE.txt` 에 있다.
