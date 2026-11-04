# DDGI 로드맵

probe 로 diffuse 간접광을 구하는 DDGI 를 scene rendering pipeline 에 넣는다. 작업은 WEEK 3 발표 (`docs/펄어비스 인턴 WEEK 3.pdf`) 의 WEEK 4 계획을 따르고, 11/11 (수) 16:00 에 WEEK 5 로 발표한다. 원래 금요일(11/6)에 예정돼 있던 WEEK 4 발표는 생략됐다.

## 작업

1. Dynamic GI 를 pipeline 에 통합한다. 직접광만 계산하던 pipeline 이 indirect irradiance 로 diffuse 간접광도 계산한다. pipeline 은 indirect irradiance 를 먼저 path tracer 에서 받는다.
2. probe 가 주변에서 오는 빛을 모은다.
3. 화면의 점이 받는 indirect irradiance 를 probe 를 보간해 근사한다. 작업 3 이 끝나면 pipeline 은 probe 보간과 path tracing 중 하나를 골라 indirect irradiance 를 구할 수 있다.
4. 같은 시점에서 probe 보간과 path tracing 으로 구한 indirect irradiance 를 비교하고, 비교한 image 와 수치를 남긴다.
5. 작업 1~4 에서 모은 image 와 수치 중 발표에 쓸 것을 고르고, WEEK 6 계획을 정한 뒤 장표를 만든다.
6. 장표 내용을 학습하며 발표를 연습한 뒤 발표한다.

## 일정

| 기한 | 끝낼 작업 |
|---|---|
| 11/5 (목) | 1 |
| 11/7 (토) | 2 |
| 11/8 (일) | 3 |
| 11/9 (월) | 4 |
| 11/10 (화) | 5 |
| 11/11 (수) 16:00 | 6 |
