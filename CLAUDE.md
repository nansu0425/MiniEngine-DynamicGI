# MiniEngine-DynamicGI

프로젝트 설명, 빌드, reference 정책은 [README.md](README.md) 를 따른다.

## Reference 검색

- `references/` 는 `.gitignore` 대상이라 repo root 에서 Grep 하면 결과에 나오지 않는다. reference 를 검색할 때는 `path` 에 `references/<name>` 이하 경로를 지정한다.
- `references/RTXGI-DDGI` root 를 `path` 로 주고 `glob` 없이 Grep 하면 timeout 이 난다. 하위 디렉토리(`rtxgi-sdk`, `samples` 등)를 지정하거나 `glob` 을 함께 준다.
