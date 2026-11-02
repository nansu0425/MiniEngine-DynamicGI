"""engine 이 glTF 의 tangent 와 normal map 규약대로 shading normal 을 만드는지, 정답을 아는 test scene 으로 확인한다.

make 가 test scene 둘과 capture 명령 파일을 쓰고, ModelViewer 가 그 명령대로 capture 하면, judge 가 그 capture 를 판정한다.

- ray: Engine Tuning 의 Ray Tracing/View 가 고른 debug view 를 capture 한다. 그 debug view 와 PathTracer 는 같은 GetHitSurface
  (MiniEngine/Model/Raytracing/HitSurface.hlsli) 로 shading normal 을 얻으므로, 이 판정은 PT 가 받는 shading normal 의 판정이다.
- raster: debug view 를 끄고 scene 을 capture 한 뒤, scene open 으로 두 번째 test scene 을 열어 다시 capture 한다. start 가 쓰는 harness
  baseline 은 TAA 를 끄므로, scene 은 그 frame 의 raster 만으로 정해진다. raster 는 tangent space 가 정의되지 않는 경우만 판정한다.
  raster 가 그리는 빛은 directional light 와 emission 뿐이다 (check-raster-lighting.py). scene file 에 light 가 없어 기본 light 를 쓰면
  light 가 quad 의 뒤쪽 (z < 0) 에 있어 quad 가 검정이다. 그래서 scene 을 열 때마다 light set direction=RASTER_LIGHT_DIRECTION 으로
  light 를 quad 의 앞쪽에 둔다. RASTER_LIGHT_DIRECTION 과 surface normal 의 cos 는 0.82 이고, a 가 normal texture 로 얻는 shading normal
  과의 cos 는 0.08 이다. 그래서 shading normal 이 surface normal 인지 아닌지에 따라 raster 값이 크게 달라진다.

  python scripts/check-normal-map-convention.py make
  python scripts/viewer.py start --scene Harness/normal-map-convention/NormalMapConvention.ini
  python scripts/viewer.py run MiniEngine/ModelViewer/Harness/normal-map-convention/capture.txt
  python scripts/viewer.py stop
  python scripts/check-normal-map-convention.py judge

test scene 은 xy 평면의 quad 넷을 x 축을 따라 놓은 것이다. scene file 의 camera 는 model 중심의 +z 쪽에서 -z 를 보며 model 을 화면에 담는다 (viewer.py 의 write_scene_file).
모든 quad 가 같은 normal map 을 쓰고, 모든 texel 이 같아서 sample 위치와 filter 가
결과를 바꾸지 않는다.

  a: u 가 +x, v 가 -y 로 늘고 TANGENT 는 (1, 0, 0, 1) 이다.
  b: u 를 뒤집어 u 가 -x 로 늘고 TANGENT 는 (-1, 0, 0, -1) 이다. w 가 -1 인 경우다.
  c: a 를 doubleSided material 로 그리고 node 로 y 축을 축으로 180° 돌려 뒷면이 camera 를 향한다.
  d: a 에서 TANGENT 만 (0, 0, 1, 1) 로 바꿨다. TANGENT 의 xyz 가 NORMAL 과 평행해서 bitangent 식의 cross 가 0 이고, tangent space 가
     정의되지 않는다.

두 번째 test scene 은 첫 test scene 에서 d 의 material 의 normalTexture 만 뺀 것이다.

a, b, c 의 정답은 tangent attribute 와 bitangent 식을 쓰지 않고 quad 위의 image 방향으로 구한다. normal texture 의 +X 는 image 의
오른쪽, +Y 는 image 의 위쪽이고 (material.schema.json 의 normalTexture), UV (0, 0) 은 image 의 왼쪽 위라서
(Specification.adoc 의 Texture Coordinates 절) quad 위에서 +X 는 u 가 느는 방향, +Y 는 v 가 주는 방향이다. spec 은
doubleSided 의 뒷면에서 normal 을 뒤집으라고만 하므로 (material.schema.json 의 doubleSided, MUST) 같은 면을 반대쪽에서
본 normal, 즉 앞면의 shading normal 전체를 뒤집은 값을 c 의 정답으로 둔다. make 는 쓴 TANGENT 가 bitangent 식
(Specification.adoc 의 TANGENT 절) 으로 이 image 방향과 같은 tangent frame 을 만드는지 확인한다. 그래서 asset 은 spec 을
따르고, engine 이 spec 대로 읽으면 정답이 나온다.

d 의 정답은 surface normal 이다. TANGENT 는 xyz 가 단위 벡터이고 w 가 ±1 이면 되므로 (Specification.adoc 의 TANGENT 절) d 의 asset 도
spec 을 따른다. spec 은 tangent space 가 정의되지 않을 때 normal texture 를 어떻게 쓸지 정하지 않는다. 이 repo 는 normalTexture 가 없으면
surface normal 을 그대로 쓴다는 규칙 (Specification.adoc 의 Normal Texture 절) 을 따라, tangent space 가 정의되지 않으면 normal texture 를
쓰지 않고 surface normal 을 쓴다고 해석한다. make 는 d 의 TANGENT 가 bitangent 식의 cross 를 0 으로 만드는지 확인한다. raster 의 정답은
두 번째 test scene 의 같은 pixel 이다. 두 scene 의 d 는 같은 surface normal 로 shading 하므로 bit 단위로 같아야 한다.

판정은 아래와 같고, 하나라도 실패하면 exit 1 이다. Front Face 와 Geometry Normal capture 에서 모두 맞은 pixel 의 가장자리 EDGE_PIXELS 를
깎고 (camera ray 는 pixel 안의 임의 위치로 나가므로 가장자리는 capture 마다 다르다), 남은 pixel 을 column 이 이어진 덩어리로 나눠
왼쪽부터 a, b, c, d 로 본다. Shading Normal capture 는 덩어리를 찾는 데 쓰지 않는다. shading normal 이 NaN 이면 R8G8B8A8_UNORM 인 overlay
에 0 으로 담겨, 맞지 않은 pixel 과 구별되지 않기 때문이다. 덩어리가 넷이 아니거나 한 덩어리가 MIN_PIXELS 보다 작으면 판정하지 않고
실패한다. raster 판정도 이 덩어리의 pixel 을 본다. 두 raster capture 를 그린 camera 나 light (capture 옆의 state) 가 다르면 판정하지 않고
실패한다.

  [1] Front Face 에서 a, b, d 는 앞면, c 는 뒷면이다. scene 의 배치를 확인한다.
  [2] Geometry Normal 이 모두 (0, 0, 1) 과 성분마다 TOLERANCE 안이다. camera 가 +z 쪽에서 보는지 확인한다.
  [3] Shading Normal 이 정답과 성분마다 TOLERANCE 안이다.
  [4] raster scene 의 rgb 에 NaN 과 Inf 가 없다.
  [5] tangent space 가 정의되지 않는 d 에서, raster scene 이 두 번째 test scene 의 raster scene 과 bit 단위로 같다. 아래 전제가 하나라도
      없으면 판정하지 않고 실패한다.
      - 두 번째 test scene 의 d 가 모든 pixel 에서 빛을 받는다 (rgb 의 합이 0 보다 크다). 정답이 검정이면 d 가 어떤 normal 을 쓰든
        같은 검정이 되어 통과한다.
      - 두 번째 test scene 에서 normal texture 를 쓰는 a 와 쓰지 않는 d 의 rgb 합의 평균이 d 의 NORMAL_TEXTURE_CONTRAST 배 이상 다르다.
        shading normal 이 바뀌면 raster 값이 바뀌는지 본다. RASTER_LIGHT_DIRECTION 의 두 cos 로 보면 diffuse 가 약 90% 다르다. 그래서
        specular 가 차이를 줄여도 0.5 는 넘는다고 보고 0.5 로 정했다.
      - material 이 두 scene 에서 같은 a, b, c 에서, 두 scene 의 raster scene 이 bit 단위로 같다. 두 scene 이 d 의 material 밖에서
        같게 그려져야 d 를 bit 단위로 비교할 수 있다.

[2], [3] 의 정답은 normal map 의 texel 을 8 bit 에서 decode 한 값으로 구하므로 texel 의 양자화는 오차가 아니다. vertex normal 과 tangent 는
float32 로 담는다. 남는 오차는 overlay 의 R8G8B8A8_UNORM (NormalColor 가 normal 을 [0, 1] 로 옮겨 담으므로 normal 로 약 1/255)
이다. TOLERANCE 는 그 몇 배다. 규약이 하나라도 틀리면 한 성분의 부호가 바뀌어 0.9 이상 다르다.
"""

import argparse
import json
import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gltfwriter import GLTF_FLOAT, GLTF_UNSIGNED_SHORT, BufferBuilder, flatten
from imagefiles import ImageFileError, read_dds_rgba32f
from imagemasks import column_groups, eroded
from viewer import HARNESS_DIR, SETTLE_FRAMES, write_scene_file

SCENE_DIR = HARNESS_DIR / 'normal-map-convention'
GLTF_NAME = 'NormalMapConvention.gltf'
# 두 번째 test scene. tangent space 가 정의되지 않는 quad 의 material 에서 normalTexture 만 뺐다.
SURFACE_NORMAL_GLTF_NAME = 'NormalMapConventionSurfaceNormal.gltf'
CAPTURES = {'front face': 'Front Face', 'geometry normal': 'Geometry Normal', 'shading normal': 'Shading Normal'}
QUAD_FINDING_CAPTURES = ['front face', 'geometry normal']
RASTER_CAPTURE = 'raster'
SURFACE_NORMAL_RASTER_CAPTURE = 'raster-surface-normal'
# surface 에서 light 를 향한 방향이다. app 이 normalize 한다.
RASTER_LIGHT_DIRECTION = (-1.0, -1.0, 2.0)
NORMAL_TEXTURE_CONTRAST = 0.5

# 세 성분이 모두 0 이 아니어야 tangent, bitangent, normal 중 어느 축이 틀려도 결과가 바뀐다.
NORMAL_TEXEL = (189, 204, 209)
NORMAL_TEXTURE_SIZE = 4

EDGE_PIXELS = 2
MIN_PIXELS = 500
TOLERANCE = 0.02

CAMERA_SIDE = np.array([0.0, 0.0, 1.0])
FRONT_FACE_COLOR = (0, 255, 0)
BACK_FACE_COLOR = (255, 0, 0)

QUAD_POSITIONS = [(-1.0, 1.0, 0.0), (1.0, 1.0, 0.0), (1.0, -1.0, 0.0), (-1.0, -1.0, 0.0)]
QUAD_NORMAL = (0.0, 0.0, 1.0)
# glTF 의 앞면은 counter-clockwise 다. +z 에서 보면 두 triangle 모두 counter-clockwise 다.
QUAD_INDICES = [0, 3, 2, 0, 2, 1]

QUADS = [
    SimpleNamespace(name='a', uv=[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], tangent=(1.0, 0.0, 0.0, 1.0), tangent_space_defined=True,
                    double_sided=False, translation=(-3.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)),
    SimpleNamespace(name='b', uv=[(1.0, 0.0), (0.0, 0.0), (0.0, 1.0), (1.0, 1.0)], tangent=(-1.0, 0.0, 0.0, -1.0), tangent_space_defined=True,
                    double_sided=False, translation=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)),
    SimpleNamespace(name='c', uv=[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], tangent=(1.0, 0.0, 0.0, 1.0), tangent_space_defined=True,
                    double_sided=True, translation=(3.0, 0.0, 0.0), rotation=(0.0, 1.0, 0.0, 0.0)),
    SimpleNamespace(name='d', uv=[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], tangent=(0.0, 0.0, 1.0, 1.0), tangent_space_defined=False,
                    double_sided=False, translation=(6.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)),
]


def normalized(vector):
    return vector / np.linalg.norm(vector)


def rotation_matrix(quaternion):
    x, y, z, w = quaternion
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def image_directions(quad):
    """quad 의 local space 에서 normal texture 의 +X (u 가 느는 쪽) 와 +Y (v 가 주는 쪽)."""
    p0, p1, p2 = (np.array(QUAD_POSITIONS[i]) for i in QUAD_INDICES[:3])
    t0, t1, t2 = (np.array(quad.uv[i]) for i in QUAD_INDICES[:3])
    (du1, dv1), (du2, dv2) = t1 - t0, t2 - t0
    e1, e2 = p1 - p0, p2 - p0
    determinant = du1 * dv2 - du2 * dv1
    position_per_u = (e1 * dv2 - e2 * dv1) / determinant
    position_per_v = (e2 * du1 - e1 * du2) / determinant
    return normalized(position_per_u), normalized(-position_per_v)


def texture_normal():
    """engine 처럼 texel 을 [-1, 1] 로 decode 하고 normalize 한 tangent space normal."""
    return normalized(np.array(NORMAL_TEXEL) / 255.0 * 2.0 - 1.0)


def expected_normals(quad):
    """(geometry normal, shading normal) 의 정답. 둘 다 HitSurface 처럼 ray 가 온 쪽을 향한다."""
    rotation = rotation_matrix(quad.rotation)
    right, up = (rotation @ direction for direction in image_directions(quad))
    normal = rotation @ np.array(QUAD_NORMAL)
    x, y, z = texture_normal()
    shading = normalized(x * right + y * up + z * normal) if quad.tangent_space_defined else normal
    side = 1.0 if normal @ CAMERA_SIDE > 0.0 else -1.0
    return side * normal, side * shading


def is_seen_from_front(quad):
    return rotation_matrix(quad.rotation) @ np.array(QUAD_NORMAL) @ CAMERA_SIDE > 0.0


def tangent_frame_problems(quad):
    """TANGENT 와 bitangent 식이 만드는 tangent frame 이 quad 가 판정하려는 frame 과 다른 이유. tangent space 가 정의되는 quad 에서는
    frame 이 image 방향과 다르면 asset 이 spec 을 따르지 않는다. 정의되지 않는 quad 에서는 bitangent 식의 cross 가 0 이어야 한다."""
    right, up = image_directions(quad)
    tangent = np.array(quad.tangent[:3])
    cross = np.cross(np.array(QUAD_NORMAL), tangent)
    if not quad.tangent_space_defined:
        return [f'quad {quad.name}: cross(NORMAL, TANGENT) {cross} is not 0, so the tangent space is defined'] if cross.any() else []
    bitangent = cross * quad.tangent[3]
    problems = []
    if not np.allclose(tangent, right):
        problems.append(f'quad {quad.name}: TANGENT {tangent} is not +u {right}')
    if not np.allclose(bitangent, up):
        problems.append(f'quad {quad.name}: cross(NORMAL, TANGENT) * w {bitangent} is not -v {up}')
    return problems


def write_test_scene(gltf_name, has_normal_texture):
    """QUADS 의 glTF 와 scene file 을 SCENE_DIR 에 쓰고 scene file 의 경로를 돌려준다. has_normal_texture(quad) 가 거짓인 quad 의
    material 에는 normalTexture 를 넣지 않는다."""
    builder = BufferBuilder()
    vertex_count = len(QUAD_POSITIONS)
    position = builder.add(flatten(QUAD_POSITIONS), GLTF_FLOAT, 'VEC3', vertex_count, with_bounds=True)
    normal = builder.add(flatten([QUAD_NORMAL] * vertex_count), GLTF_FLOAT, 'VEC3', vertex_count)
    indices = builder.add(QUAD_INDICES, GLTF_UNSIGNED_SHORT, 'SCALAR', len(QUAD_INDICES))

    materials = []
    meshes = []
    nodes = []
    for quad in QUADS:
        uv = builder.add(flatten(quad.uv), GLTF_FLOAT, 'VEC2', vertex_count)
        tangent = builder.add(flatten([quad.tangent] * vertex_count), GLTF_FLOAT, 'VEC4', vertex_count)
        material = {
            'name': f'quad {quad.name}',
            'pbrMetallicRoughness': {'baseColorFactor': [1.0, 1.0, 1.0, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 1.0},
            'doubleSided': quad.double_sided,
        }
        if has_normal_texture(quad):
            material['normalTexture'] = {'index': 0}
        materials.append(material)
        meshes.append({'name': f'quad {quad.name}', 'primitives': [{
            'attributes': {'POSITION': position, 'NORMAL': normal, 'TANGENT': tangent, 'TEXCOORD_0': uv},
            'indices': indices, 'material': len(materials) - 1}]})
        nodes.append({'name': f'quad {quad.name}', 'mesh': len(meshes) - 1, 'translation': list(quad.translation),
                      'rotation': list(quad.rotation)})

    gltf_path = SCENE_DIR / gltf_name
    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/check-normal-map-convention.py'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'textures': [{'source': 0}],
        'images': [{'uri': 'normal.png'}],
        'buffers': [{'uri': gltf_path.with_suffix('.bin').name, 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    gltf_path.with_suffix('.bin').write_bytes(builder.data)
    gltf_path.write_text(json.dumps(gltf, indent=2), encoding='utf-8')
    return write_scene_file(gltf_path)


def command_make(args):
    problems = [problem for quad in QUADS for problem in tangent_frame_problems(quad)]
    if problems:
        for problem in problems:
            print(f'error: {problem}', file=sys.stderr)
        return 1

    SCENE_DIR.mkdir(parents=True, exist_ok=True)
    texture = np.full((NORMAL_TEXTURE_SIZE, NORMAL_TEXTURE_SIZE, 3), NORMAL_TEXEL, dtype=np.uint8)
    Image.fromarray(texture).save(SCENE_DIR / 'normal.png')
    scene = write_test_scene(GLTF_NAME, lambda quad: True)
    surface_normal_scene = write_test_scene(SURFACE_NORMAL_GLTF_NAME, lambda quad: quad.tangent_space_defined)

    # scene open 은 light 를 scene file 의 값으로 되돌리므로 scene 을 열 때마다 light 를 둔다.
    raster_light = 'light set direction=' + ','.join(f'{value:g}' for value in RASTER_LIGHT_DIRECTION)
    lines = ['ui hide', raster_light]
    for name, view in CAPTURES.items():
        lines += [f'settings set Engine Tuning: Ray Tracing/View=Debug: {view}', f'frames {SETTLE_FRAMES}',
                  f'capture overlay {capture_path(name).with_suffix("")}']
    lines += ['settings set Engine Tuning: Ray Tracing/View=Off', f'frames {SETTLE_FRAMES}',
              f'capture scene {raster_capture_path(RASTER_CAPTURE).with_suffix("")}',
              f'scene open {surface_normal_scene}', raster_light, f'frames {SETTLE_FRAMES}',
              f'capture scene {raster_capture_path(SURFACE_NORMAL_RASTER_CAPTURE).with_suffix("")}']
    (SCENE_DIR / 'capture.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    print(f'scene files: {scene}, {surface_normal_scene}')
    print(f'capture commands: {SCENE_DIR / "capture.txt"}')
    return 0


def capture_path(name):
    return SCENE_DIR / f'{name.replace(" ", "-")}.png'


def raster_capture_path(name):
    return SCENE_DIR / f'{name}.dds'


def state_path(capture):
    return capture.with_suffix('.state.json')


def decoded_normals(capture, group):
    return capture[group][:, :3].astype(np.float64) / 255.0 * 2.0 - 1.0


def normal_problems(capture, group, expected):
    normals = decoded_normals(capture, group)
    error = float(np.abs(normals - expected).max())
    summary = f'mean {np.round(normals.mean(axis=0), 4).tolist()}, expected {np.round(expected, 4).tolist()}, max error {error:.4f}'
    return summary, [] if error <= TOLERANCE else [f'max error {error:.4f} is more than {TOLERANCE}']


def raster_finite_problems(raster, group):
    nonfinite = int(np.count_nonzero(~np.isfinite(raster[group]).all(axis=1)))
    return f'{nonfinite} of {int(group.sum())} pixels have NaN or Inf', [f'{nonfinite} pixels have NaN or Inf'] if nonfinite else []


def bit_different_pixels(raster, answer, group):
    return int(np.count_nonzero(np.any(raster[group].view(np.uint32) != answer[group].view(np.uint32), axis=1)))


def raster_bit_problems(raster, answer, group):
    different = bit_different_pixels(raster, answer, group)
    summary = f'{different} of {int(group.sum())} pixels differ in bits from the second test scene'
    return summary, [f'{different} pixels differ'] if different else []


def raster_comparison_premises(raster, answer, quad_groups):
    """[5] 의 전제마다 (있는지, 설명)."""
    undefined = [quad.name for quad in QUADS if not quad.tangent_space_defined]
    defined = [quad.name for quad in QUADS if quad.tangent_space_defined]
    premises = []
    for name in undefined:
        rgb_sums = answer[quad_groups[name]].astype(np.float64).sum(axis=1)
        unlit = int(np.count_nonzero(~(rgb_sums > 0.0)))
        premises.append((unlit == 0, f'quad {name} of the second test scene is lit: {unlit} of {len(rgb_sums)} pixels have rgb sum 0 or less, '
                                     f'mean rgb sum {rgb_sums.mean():.6g}'))
        textured, untextured = (float(answer[quad_groups[other]].astype(np.float64).sum(axis=1).mean()) for other in (defined[0], name))
        contrast = abs(textured - untextured) / untextured if untextured > 0.0 else 0.0
        premises.append((contrast >= NORMAL_TEXTURE_CONTRAST,
                         f'quad {defined[0]} with the normal texture and quad {name} without it differ in the second test scene: mean rgb sum '
                         f'{textured:.6g} and {untextured:.6g}, difference {contrast:.3f} of quad {name}, at least {NORMAL_TEXTURE_CONTRAST}'))
    different = {name: bit_different_pixels(raster, answer, quad_groups[name]) for name in defined}
    premises.append((not any(different.values()), 'quads with the same material in both test scenes are the same in bits: '
                     + ', '.join(f'quad {name} {count} pixels differ' for name, count in different.items())))
    return premises


def command_judge(args):
    raster_names = [RASTER_CAPTURE, SURFACE_NORMAL_RASTER_CAPTURE]
    try:
        captures = {name: np.asarray(Image.open(capture_path(name)).convert('RGB')) for name in CAPTURES}
        rasters = {name: np.ascontiguousarray(read_dds_rgba32f(raster_capture_path(name))[..., :3]) for name in raster_names}
        states = {name: json.loads(state_path(raster_capture_path(name)).read_text(encoding='utf-8')) for name in raster_names}
        drawn_from = {name: {key: state[key] for key in ('camera', 'light')} for name, state in states.items()}
    except (OSError, ImageFileError, KeyError, json.JSONDecodeError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    shapes = {image.shape[:2] for image in [*captures.values(), *rasters.values()]}
    if len(shapes) != 1:
        print(f'error: the captures differ in size: {shapes}', file=sys.stderr)
        return 1

    hit = np.logical_and.reduce([captures[name].any(axis=2) for name in QUAD_FINDING_CAPTURES])
    groups = column_groups(eroded(hit, EDGE_PIXELS))
    sizes = [int(group.sum()) for group in groups]
    print(f'captures: {SCENE_DIR}')
    print(f'quads found: {len(groups)} groups of {sizes} pixels after trimming {EDGE_PIXELS} edge pixels')
    if len(groups) != len(QUADS) or min(sizes, default=0) < MIN_PIXELS:
        print(f'FAIL: expected {len(QUADS)} groups of at least {MIN_PIXELS} pixels')
        return 1
    if drawn_from[RASTER_CAPTURE] != drawn_from[SURFACE_NORMAL_RASTER_CAPTURE]:
        print(f'FAIL: the raster captures were drawn with different cameras or lights: {drawn_from}')
        return 1

    failed = False
    for check, name in enumerate(CAPTURES, start=1):
        print(f'[{check}] {CAPTURES[name]}')
        for quad, group in zip(QUADS, groups):
            geometry, shading = expected_normals(quad)
            if name == 'front face':
                expected_color = FRONT_FACE_COLOR if is_seen_from_front(quad) else BACK_FACE_COLOR
                colors = captures[name][group]
                wrong = int(np.count_nonzero(np.any(colors != expected_color, axis=1)))
                summary = f'{"front" if is_seen_from_front(quad) else "back"} face expected, {wrong} of {len(colors)} pixels differ'
                problems = [f'{wrong} pixels are not {expected_color}'] if wrong else []
            else:
                summary, problems = normal_problems(captures[name], group, geometry if name == 'geometry normal' else shading)
            failed |= bool(problems)
            print(f'    {"FAIL" if problems else "PASS"}  quad {quad.name}: {summary}')

    print(f'[{len(CAPTURES) + 1}] Raster: NaN and Inf')
    for quad, group in zip(QUADS, groups):
        summary, problems = raster_finite_problems(rasters[RASTER_CAPTURE], group)
        failed |= bool(problems)
        print(f'    {"FAIL" if problems else "PASS"}  quad {quad.name}: {summary}')
    print(f'[{len(CAPTURES) + 2}] Raster: undefined tangent space against the second test scene')
    raster, answer = rasters[RASTER_CAPTURE], rasters[SURFACE_NORMAL_RASTER_CAPTURE]
    premises = raster_comparison_premises(raster, answer, {quad.name: group for quad, group in zip(QUADS, groups)})
    for present, summary in premises:
        print(f'    {"PASS" if present else "FAIL"}  premise: {summary}')
    for quad, group in zip(QUADS, groups):
        if quad.tangent_space_defined:
            continue
        if all(present for present, _ in premises):
            summary, problems = raster_bit_problems(raster, answer, group)
        else:
            summary, problems = 'not judged because a premise is missing', ['a premise is missing']
        failed |= bool(problems)
        print(f'    {"FAIL" if problems else "PASS"}  quad {quad.name}: {summary}')
    print('FAIL' if failed else 'PASS')
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('make', help=f'write the test scenes and the capture commands to {SCENE_DIR}').set_defaults(handler=command_make)
    commands.add_parser('judge', help=f'judge the captures in {SCENE_DIR}').set_defaults(handler=command_judge)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
