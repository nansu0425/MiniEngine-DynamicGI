# MiniEngine-DynamicGI

프로젝트 설명, 빌드, reference 정책은 [README.md](README.md) 를 따른다.

## Command line build

- `MSBuild\Current\Bin\amd64\MSBuild.exe` 에 `-p:PreferredToolArchitecture=x64` 를 붙여 VS IDE 와 같은 x64 host compiler 로 build 한다. 32-bit `MSBuild.exe` 는 x86 host compiler 를 쓰고, 그 compiler 가 만든 pch 는 IDE 의 compiler 와 호환되지 않아 IDE build 가 C1853 으로 실패한다.

## Reference 검색

- `references/` 는 `.gitignore` 대상이라 repo root 에서 Grep 하면 결과에 나오지 않는다. reference 를 검색할 때는 `path` 에 `references/<name>` 이하 경로를 지정한다.
