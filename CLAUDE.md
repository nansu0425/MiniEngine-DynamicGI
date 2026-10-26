# MiniEngine-DynamicGI

프로젝트 설명, 빌드, reference 정책은 [README.md](README.md) 를 따른다.

## App build, 실행, 확인

- app 의 build, 실행, 확인은 `modelviewer` skill ([.claude/skills/modelviewer/SKILL.md](.claude/skills/modelviewer/SKILL.md)) 로 한다. 확인을 사용자에게 부탁하기 전에 harness 로 먼저 확인한다.

## 완료 판정 기준

완료 기준은 작업이 이루려던 결과가 나왔는지를 확인하는 것이다. 그래서 code 를 바꾸는 작업은 결과부터 빠짐없이 정의한다. 결과는 바꾸려는 것만이 아니라 작업 뒤 code 가 있어야 할 상태 전체다.

- 바뀌는 것: 작업이 바꾸려는 동작.
- 그대로인 것: 지우거나 바꾸는 code 가 내놓는 것 (반환값, 파일, buffer, 상태) 을 쓰는 code 를 모두 찾아, 하나하나 그 동작이 그대로여야 하는지 함께 바뀌어야 하는지 정한다.
- 조건: 결과는 바꾼 code 가 실제로 도는 조건 (build config, 설정, 장면, 시점, 입력) 에서 성립해야 한다. 판정하는 조건이 그와 다르면 그 차이 때문에 놓칠 수 있는 것을 적고, 기준을 더하거나 한계로 보고한다. 예를 들어 harness baseline 은 결정성을 위해 TAA, motion blur, adaptive exposure 를 끄므로, rendering 이나 그 입력을 바꾸는 작업은 modelviewer skill 의 "변경 전후 비교" 를 기준에 넣는다.

기준마다 무엇을 어떤 조건에서 재고 어떤 값이면 통과인지 적는다. `viewer.py selftest` 는 harness 자체의 확인이므로 rendering 의 회귀 기준으로 쓰지 않는다. 완료를 보고할 때는 판정을 어떤 조건에서 했는지와 다루지 않은 조건을 함께 적는다.

## Reference 검색

- `references/` 는 `.gitignore` 대상이라 repo root 에서 Grep 하면 결과에 나오지 않는다. reference 를 검색할 때는 `path` 에 `references/<name>` 이하 경로를 지정한다.
