# DDGI 로드맵

probe 로 diffuse 간접광을 구하는 DDGI 를 scene rendering pipeline 에 넣는다. 작업은 WEEK 3 발표 (`docs/펄어비스 인턴 WEEK 3.pdf`) 의 WEEK 4 계획을 따르고, 11/6 (금) 에 WEEK 4 로 발표한다.

## 작업

1. Dynamic GI 를 pipeline 에 통합한다. 직접광만 계산하던 pipeline 이 diffuse 간접광도 계산한다.
2. probe 로 diffuse 간접광을 구한다. 화면의 점이 받는 간접광의 양을 probe 를 보간해 근사한다.
3. probe 근사와 path tracing 을 비교한다. 간접광의 양을 계산하는 방식만 바꿔 비교한다.
   - (a) 비교할 시점마다 path tracing 으로 간접광의 양을 쌓는다.
   - (b) probe 근사로 구한 간접광의 양을 (a) 와 비교한다.
4. 발표 자료를 정리하고 WEEK 5 계획을 정한다.
5. 장표를 만들고 발표한다.

## 일정

| 기한 | 끝낼 작업 |
|---|---|
| 11/3 (화) | 1 |
| 11/4 (수) | 2, 3 (a) |
| 11/5 (목) | 3 (b), 4 |
| 11/6 (금) | 5 |
