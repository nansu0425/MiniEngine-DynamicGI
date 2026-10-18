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

2. `MiniEngine/ModelViewer/ModelViewer.slnx` 를 열고 build 후 실행한다. DirectXTex, DirectXMesh 는 첫 build 때 vcpkg 가 받는다.

조작법은 [MiniEngine/readme.md](MiniEngine/readme.md) 에 있다.

## 코드 규칙

- 이 프로젝트에서 작성하거나 수정한 코드의 주석은 한국어로 쓰고, 기술 용어는 영어로 쓴다. upstream 코드의 주석은 그대로 둔다.
- source 파일은 UTF-8 로 저장한다. editor 는 [.editorconfig](.editorconfig), MSVC 는 `/utf-8` ([Build.props](MiniEngine/PropertySheets/Build.props)) 로 이를 따른다.

## License

이 프로젝트의 작업은 MIT License 를 따른다([LICENSE](LICENSE)). upstream MiniEngine 코드는 Microsoft 의 MIT License 를 따른다([MiniEngine/License.txt](MiniEngine/License.txt)). Sponza asset 의 출처는 받은 뒤 `MiniEngine/ModelViewer/Sponza/pbr/copyright.txt` 에 있다.
