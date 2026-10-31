# Scene object 의 transform 설계

scene object 를 component 로 조립하는 구조, Model 과 Sky 의 이름, light 의 Type, Euler 각과 quaternion 의 변환 module 은 e353f0c ~ dfe3984 에서 끝났다. 끝난 구조는 code (`MiniEngine/Core/Scene/Scene.h`, `MiniEngine/Core/Math/Rotation.h`, `MiniEngine/ModelViewer/ModelViewer.cpp`) 가 진실이다. 이 문서에는 아직 하지 않은 transform 작업의 설계와, 사용자가 승인한 결정만 둔다. 측정 scene 교체를 먼저 하기로 해서 이 작업은 미뤘다.

## 목표 구조

```
Object  name="Model", transform
  └─ ModelComponent   glTF 경로
Object  name="Camera", transform
  └─ CameraComponent  Fit=Model
Object  name="Directional Light", transform
  ├─ LightComponent   Type, Color, Intensity
  └─ IconComponent
Object  name="Sky", transform
  └─ SkyComponent     Color, Intensity
```

- 모든 object 가 transform 을 갖는다. transform 의 구조와 조작 방식은 모든 object 에서 같다. 효과는 object 마다 다르다.
- 한 값은 한 곳에만 둔다. transform 은 Object 만 갖는다. renderer 에 넘기는 사본 (예: `ModelInstance::m_Locator`) 은 transform 이 바뀔 때 다시 넘긴다.
- `AddComponent` 가 component 에 소유 object 를 알려 준다. transform 이 필요한 component 는 소유 object 의 transform 을 읽는다.
- Object 의 key 와 component 의 key 가 겹치면 ASSERT 로 멈춘다.
- CameraComponent: `EditorCameraController` 는 mouse 와 keyboard 로 Camera object 의 transform 을 옮긴다. controller 는 pose 를 따로 갖지 않는다.
- IconComponent: 스스로 보이지 않는 object 를 scene 위에 icon 으로 표시하고, icon 을 클릭해 고르게 한다. type 과 무관하게 소유 object 의 transform 위치에 그린다. icon 의 그림과 -Z 방향 화살표를 그릴지는 생성 인자로 받는다.

## transform 의 data 와 규약

- Object 의 transform 은 `Position` (world 좌표), `Rotation` (단위 quaternion), `Scale` (uniform scale 하나) 이다. `Math::UniformTransform` 과 같은 구성이다. `Rotation` 은 glTF node 처럼 x, y, z, w 순서로 쓰고, w ≥ 0 인 쪽을 쓴다 (`Math::GetCanonicalQuaternion`).
- Euler 각은 Details 와 명령에서만 쓰는 표현이다. 축 순서는 intrinsic Y-X-Z, 곧 `R = Ry(Y) · Rx(X) · Rz(Z)` 이다. Details 는 degree 로, `state camera` 와 `pose` 는 지금처럼 radian 으로 쓴다.
- camera 는 glTF 처럼 local -Z 를 보고 +Y 가 위다. 그래서 Y = heading, X = pitch, Z = roll 이다. Y-X-Z 순서는 지금 camera 의 `Ry(heading) · Rx(pitch)` 와 같으므로 roll 0 인 pose 의 뜻이 바뀌지 않는다. roll 양수는 local +Z 를 축으로 반시계 방향이라, camera 의 위쪽이 화면 왼쪽으로 기운다.
- directional light 는 KHR_lights_punctual 대로 local -Z 로 빛이 나아간다. `state light` 의 `direction` 은 지금처럼 surface 에서 light 를 향하는 local +Z 다. roll 은 빛을 바꾸지 않는다. `elevation = -X` 이고 `azimuth = (90° - Y) mod 360` 이다. `light set elevation= azimuth=` 와 `direction=` 은 roll 0 인 Euler 로 rotation 을 정한다.
- Scale 이 효과를 갖는 object 는 Model 뿐이다. glTF 는 camera 의 view matrix 를 rotation 과 translation 으로만 만들고, directional light 는 position 과 scale 을 무시한다. light 의 Position 은 조명에 효과가 없지만 icon 의 자리다. Sky 의 transform 은 아무 효과가 없다.

## scene file

- key 의 주인

| 주인 | key |
|---|---|
| Object | `Position`, `Rotation`, `Scale` |
| ModelComponent | `glTF`, `Radius` (open-only) |
| CameraComponent | `Fit` (open-only) |
| LightComponent | `Type`, `Color`, `Intensity` |
| SkyComponent | `Color`, `Intensity` |

- transform 의 key 는 기본값이어도 늘 쓴다. `editor set` 은 `editor get` 이 준 key 만 받으므로, 기본값이라 생략한 key 는 `editor set` 으로 바꿀 수 없다.
- open-only: `[Model] Radius` 는 열 때 Scale 로 바꾸고, `[Camera] Fit=Model` 은 열 때 Position 과 Rotation 으로 바꾼다. 저장하면 두 key 의 줄을 지우고 계산한 값을 쓴다. Fit=Model 은 Model 의 transform 을 받은 뒤에 계산한다.
- 옛 key (`[Camera] Heading`, `Pitch`, `[Directional Light] Direction`, `Up`) 는 읽지 않는다. repo 의 scene file 은 같은 commit 에서 옮긴다. 지금 `[Model] Scale` 의 뜻 (glTF 좌표에 곱하는 배율) 은 Model 의 transform Scale 과 같다.
- quaternion 을 거치면 camera 와 light 의 방향이 float 몇 ulp 바뀐다. Cornell box 의 camera 처럼 rotation 이 identity 인 경우만 bit 단위로 같다.

## Details 와 Euler hint

- Details 맨 위의 Transform 에서 Position 과 Rotation (Euler, degree) 은 `DragFloat3` 이고, Scale 은 `DragFloat` 이다. 값 위에서 좌우로 끌어 바꾼다.
- Object 는 quaternion 옆에 Euler hint 를 둔다. Details 는 quaternion 을 매 frame 분해하지 않고 hint 를 표시한다. Euler 를 받는 곳 (Details, `pose`, camera 의 mouse look) 은 hint 를 그대로 두고 quaternion 을 hint 로 만든다. 그래서 끄는 동안 값이 ±180° 에서 뛰거나 pitch ±90° 에서 뒤집히지 않는다.
- quaternion 을 받는 곳 (gizmo, scene file, `editor set Rotation`, `light set rotation`) 은 hint 를 다시 정한다. 분해의 두 해 `(X, Y, Z)` 와 `(180°-X, Y+180°, Z+180°)` 에 각각 360° 의 배수를 더해, 앞의 hint 에 가장 가까운 것을 고른다. |X| = 90° 이면 앞의 Y 를 두고 Z 만 푼다. hint 는 scene file 에 쓰지 않는다. 열 때는 X ∈ [-90°, 90°], Y, Z ∈ (-180°, 180°] 로 분해한다.
- camera 는 rotation 을 늘 Euler (heading, pitch, roll) 에서 만든다. quaternion 을 받으면 먼저 hint 로 분해하고 분해한 hint 로 rotation 을 다시 만든다. 그래야 측정 조건의 줄에 쓴 heading, pitch, roll 을 Restore 하면 같은 matrix 가 된다. camera 의 pitch 는 ±90° 로 제한한다.

## gizmo

- W translate, E rotate, R scale 이다. toolbar 에 Scale (R) 을 추가하고 `editor mode translate|rotate|scale` 로 바꾼다. Local/World 는 translate 와 rotate 에 적용한다. scale 은 uniform 이므로 ImGuizmo 의 `SCALEU` 를 쓴다. `state editor` 의 `rotate_axes` 는 `axes` 로 이름을 바꾼다.
- gizmo 는 고른 object 의 transform 원점에 그린다. Camera 를 뺀 모든 object 가 gizmo 를 갖는다. Camera 는 화면을 그리는 camera 라서 gizmo 가 near clip 안에 놓인다. icon 이 없는 Model 과 Sky 는 Scene Hierarchy 로 고르면 gizmo 가 생긴다.

## camera roll

- mouse look 은 heading 과 pitch 만 바꾸고 roll 을 유지한다. WASD 와 가운데 button 의 pan 은 roll 이 들어간 축을 따르고, E, Q 는 world 의 위아래다. `LookAt` 은 roll 을 유지한다.
- `state camera` 와 `pose` 의 응답에 `roll` (radian) 을 추가한다. `pose <x> <y> <z> <heading> <pitch> [<roll>]` 에서 roll 이 없으면 0 이다.
- 측정 조건의 줄에 `roll` 을 추가한다. roll 이 없는 옛 줄은 roll 0 으로 읽는다.
- 측정 조건의 줄에서 light 는 `light direction` 과 `light up` 을 쓰지 않고 `light rotation=<x>,<y>,<z>,<w>` 를 쓴다. 문서와 kept 에 기록한 옛 줄을 Restore 하도록 옛 두 key 도 읽는다.

## 바뀌는 harness 명령

- `light set rotation=` 은 그대로다. `direction=`, `elevation=`, `azimuth=` 는 roll 0 인 Euler 로 rotation 을 정하는 편의 형식으로 남는다. `position=` 은 transform 의 Position 이다. `light reset` 은 transform 을 identity 로 둔다.
- `editor get/set` 의 key

| object | 지금 | 바뀐 뒤 |
|---|---|---|
| Model | (없음) | `glTF`, `Position`, `Rotation`, `Scale` |
| Camera | `Heading`, `Pitch`, `Position` | `Position`, `Rotation`, `Scale` |
| Directional Light | `Type`, `Color`, `Direction`, `Intensity`, `Position`, `Up` | `Type`, `Color`, `Intensity`, `Position`, `Rotation`, `Scale` |
| Sky | `Color`, `Intensity` | `Color`, `Intensity`, `Position`, `Rotation`, `Scale` |

## 단계

1. 모든 scene object 의 transform 을 Object 에 두고 scene file 에 Position, Rotation, Scale 로 저장한다. scene file, script, check, 측정 조건의 light 줄을 함께 옮기고, IconComponent 를 type 과 무관하게 바꾼다.
2. Details panel 이 transform 을 Euler 각으로 표시하고 끌어서 바꾼다 (hint).
3. gizmo 로 Camera 를 뺀 모든 object 를 옮기고 돌리고 크기를 바꾼다 (W, E, R, translate 의 Local/World).
4. camera 가 roll 을 갖는다 (controller, `state camera`, `pose`, 측정 조건의 줄).

## 시작할 때 정할 것

- scene file 의 `Rotation` 으로 camera 를 열려면 quaternion 을 heading, pitch 로 분해해야 하고, mouse look 은 heading 과 pitch 를 바꾼다. 그래서 Euler hint 를 1 단계에 넣을지, 2 단계에 둘 수 있는지 정한다.
- 1 단계 뒤 2 단계 전까지 Details 가 무엇을 표시하고 바꾸는지 정한다. 지금 camera 의 Details 는 Position, Heading, Pitch 를 바꾸고, light 의 Details 에는 Reset Transform 이 있다.
- `light set direction=` 을 roll 0 인 Euler 로 정한 rotation 이 지금 `MakeRotationToward` 의 결과와 같은지 수식으로 확인한다.
- `Math::MakeEulerRotation` 의 `base` 인자는 dfe3984 에서 bit 를 지키려고 남겼다. ulp 차이를 받아들이는 1 단계에서 없앨지 정한다.
- 1 단계가 한 commit 에 크면 Model, light, camera 로 나눌지 정한다.

## 사용자가 승인한 결정

- SceneComponent 를 두지 않고 Object 가 이름과 transform 을 갖는다.
- directional, point, spot 은 LightComponent 하나와 Type 으로 다룬다. Type 은 지금 Directional 만 받는다.
- 모든 object 가 Scale 을 갖고 바꿀 수 있으며, Sky 도 transform 을 갖는다.
- Details 는 component type 마다 SceneEditor 에 등록한 함수로 그린다.
- 옛 key (Heading, Pitch, Direction, Up) 는 scene file 에서 읽지 않는다.
- 측정 조건의 줄의 light 는 `light rotation` 으로 바꾸고, 옛 두 key 도 읽는다.
- 기본값인 transform key 도 늘 쓴다.
- translate 도 Local/World 를 따르고, `rotate_axes` 를 `axes` 로 바꾼다.
- 값은 ImGui 기본인 좌우 drag 로 끈다.
- roll 이 없는 `pose` 는 roll 0 이고, `pose look-at` 은 roll 을 유지한다.
- camera 의 pitch 는 ±90° 로 제한한다.
- baseline 의 camera 와 light 가 quaternion 을 거치며 ulp 단위로 바뀌는 것을 받아들인다. 승인할 때의 근거는 "측정 scene 을 곧 새 구도로 바꾸고 snapshot 도 다시 저장한다" 였다. 측정 scene 교체를 먼저 하므로, 이 작업을 시작할 때 snapshot 을 한 번 더 저장한다.
- mesh 를 클릭해 Model 을 고르는 기능은 이번에 넣지 않는다.
