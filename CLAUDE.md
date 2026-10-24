# MiniEngine-DynamicGI

프로젝트 설명, 빌드, reference 정책은 [README.md](README.md) 를 따른다.

## App build, 실행, 확인

- app 의 build, 실행, 확인은 `modelviewer` skill ([.claude/skills/modelviewer/SKILL.md](.claude/skills/modelviewer/SKILL.md)) 로 한다. 확인을 사용자에게 부탁하기 전에 harness 로 먼저 확인한다.

## Reference 검색

- `references/` 는 `.gitignore` 대상이라 repo root 에서 Grep 하면 결과에 나오지 않는다. reference 를 검색할 때는 `path` 에 `references/<name>` 이하 경로를 지정한다.
