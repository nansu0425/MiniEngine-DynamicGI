"""engine 이 glTF 의 tangent 와 normal map 규약대로 shading normal 을 만드는지, 정답을 아는 test scene 으로 확인한다.

make 가 test scene 과 capture 명령 파일을 쓰고, ModelViewer 가 Ray Tracing View 를 capture 하면, judge 가 그 capture 를
판정한다. Ray Tracing View 와 PathTracer 는 같은 GetHitSurface (MiniEngine/Model/Raytracing/HitSurface.hlsli) 로 shading
normal 을 얻으므로, 이 판정은 PT 가 받는 shading normal 의 판정이다.

  python scripts/check-normal-map-convention.py make
  python scripts/viewer.py start --scene Harness/normal-map-convention/NormalMapConvention.ini
  python scripts/viewer.py run MiniEngine/ModelViewer/Harness/normal-map-convention/capture.txt
  python scripts/viewer.py stop
  python scripts/check-normal-map-convention.py judge

test scene 은 xy 평면의 quad 셋을 x 축을 따라 놓은 것이다. scene file 의 camera 는 model 중심의 +z 쪽에서 -z 를 보며 model 을 화면에 담는다 (viewer.py 의 write_scene_file).
모든 quad 가 같은 normal map 을 쓰고, 모든 texel 이 같아서 sample 위치와 filter 가
결과를 바꾸지 않는다.

  a: u 가 +x, v 가 -y 로 늘고 TANGENT 는 (1, 0, 0, 1) 이다.
  b: u 를 뒤집어 u 가 -x 로 늘고 TANGENT 는 (-1, 0, 0, -1) 이다. w 가 -1 인 경우다.
  c: a 를 doubleSided material 로 그리고 node 로 y 축을 축으로 180° 돌려 뒷면이 camera 를 향한다.

정답은 tangent attribute 와 bitangent 식을 쓰지 않고 quad 위의 image 방향으로 구한다. normal texture 의 +X 는 image 의
오른쪽, +Y 는 image 의 위쪽이고 (material.schema.json 의 normalTexture), UV (0, 0) 은 image 의 왼쪽 위라서
(Specification.adoc 의 Texture Coordinates 절) quad 위에서 +X 는 u 가 느는 방향, +Y 는 v 가 주는 방향이다. spec 은
doubleSided 의 뒷면에서 normal 을 뒤집으라고만 하므로 (material.schema.json 의 doubleSided, MUST) 같은 면을 반대쪽에서
본 normal, 즉 앞면의 shading normal 전체를 뒤집은 값을 c 의 정답으로 둔다. make 는 쓴 TANGENT 가 bitangent 식
(Specification.adoc 의 TANGENT 절) 으로 이 image 방향과 같은 tangent frame 을 만드는지 확인한다. 그래서 asset 은 spec 을
따르고, engine 이 spec 대로 읽으면 정답이 나온다.

판정은 아래와 같고, 하나라도 실패하면 exit 1 이다. 세 capture 에서 모두 맞은 pixel 의 가장자리 EDGE_PIXELS 를 깎고 (camera
ray 는 pixel 안의 임의 위치로 나가므로 가장자리는 capture 마다 다르다), 남은 pixel 을 column 이 이어진 덩어리로 나눠
왼쪽부터 a, b, c 로 본다. 덩어리가 셋이 아니거나 한 덩어리가 MIN_PIXELS 보다 작으면 판정하지 않고 실패한다.

  [1] Front Face 에서 a, b 는 앞면, c 는 뒷면이다. scene 의 배치를 확인한다.
  [2] Geometry Normal 이 모두 (0, 0, 1) 과 성분마다 TOLERANCE 안이다. camera 가 +z 쪽에서 보는지 확인한다.
  [3] Shading Normal 이 정답과 성분마다 TOLERANCE 안이다.

정답은 normal map 의 texel 을 8 bit 에서 decode 한 값으로 구하므로 texel 의 양자화는 오차가 아니다. 남는 오차는 overlay 의
R8G8B8A8_UNORM (NormalColor 가 normal 을 [0, 1] 로 옮겨 담으므로 normal 로 약 1/255) 과 vertex normal, tangent 의
R10G10B10A2_UNORM (약 1/1023) 이다. TOLERANCE 는 그 합의 몇 배다. 규약이 하나라도 틀리면 한 성분의 부호가 바뀌어 0.9 이상
다르다.
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
from imagemasks import column_groups, eroded
from viewer import HARNESS_DIR, SETTLE_FRAMES, write_scene_file

SCENE_DIR = HARNESS_DIR / 'normal-map-convention'
GLTF_NAME = 'NormalMapConvention.gltf'
CAPTURES = {'front face': 'Front Face', 'geometry normal': 'Geometry Normal', 'shading normal': 'Shading Normal'}

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
    SimpleNamespace(name='a', uv=[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], tangent=(1.0, 0.0, 0.0, 1.0),
                    double_sided=False, translation=(-3.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)),
    SimpleNamespace(name='b', uv=[(1.0, 0.0), (0.0, 0.0), (0.0, 1.0), (1.0, 1.0)], tangent=(-1.0, 0.0, 0.0, -1.0),
                    double_sided=False, translation=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)),
    SimpleNamespace(name='c', uv=[(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], tangent=(1.0, 0.0, 0.0, 1.0),
                    double_sided=True, translation=(3.0, 0.0, 0.0), rotation=(0.0, 1.0, 0.0, 0.0)),
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
    shading = normalized(x * right + y * up + z * normal)
    side = 1.0 if normal @ CAMERA_SIDE > 0.0 else -1.0
    return side * normal, side * shading


def is_seen_from_front(quad):
    return rotation_matrix(quad.rotation) @ np.array(QUAD_NORMAL) @ CAMERA_SIDE > 0.0


def tangent_frame_problems(quad):
    """TANGENT 와 bitangent 식이 만드는 tangent frame 이 image 방향과 다르면 asset 이 spec 을 따르지 않는다."""
    right, up = image_directions(quad)
    tangent = np.array(quad.tangent[:3])
    bitangent = np.cross(np.array(QUAD_NORMAL), tangent) * quad.tangent[3]
    problems = []
    if not np.allclose(tangent, right):
        problems.append(f'quad {quad.name}: TANGENT {tangent} is not +u {right}')
    if not np.allclose(bitangent, up):
        problems.append(f'quad {quad.name}: cross(NORMAL, TANGENT) * w {bitangent} is not -v {up}')
    return problems


def command_make(args):
    problems = [problem for quad in QUADS for problem in tangent_frame_problems(quad)]
    if problems:
        for problem in problems:
            print(f'error: {problem}', file=sys.stderr)
        return 1

    SCENE_DIR.mkdir(parents=True, exist_ok=True)
    texture = np.full((NORMAL_TEXTURE_SIZE, NORMAL_TEXTURE_SIZE, 3), NORMAL_TEXEL, dtype=np.uint8)
    Image.fromarray(texture).save(SCENE_DIR / 'normal.png')

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
        materials.append({
            'name': f'quad {quad.name}',
            'pbrMetallicRoughness': {'baseColorFactor': [1.0, 1.0, 1.0, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 1.0},
            'normalTexture': {'index': 0},
            'doubleSided': quad.double_sided,
        })
        meshes.append({'name': f'quad {quad.name}', 'primitives': [{
            'attributes': {'POSITION': position, 'NORMAL': normal, 'TANGENT': tangent, 'TEXCOORD_0': uv},
            'indices': indices, 'material': len(materials) - 1}]})
        nodes.append({'name': f'quad {quad.name}', 'mesh': len(meshes) - 1, 'translation': list(quad.translation),
                      'rotation': list(quad.rotation)})

    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/check-normal-map-convention.py'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'textures': [{'source': 0}],
        'images': [{'uri': 'normal.png'}],
        'buffers': [{'uri': 'NormalMapConvention.bin', 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    (SCENE_DIR / 'NormalMapConvention.bin').write_bytes(builder.data)
    (SCENE_DIR / GLTF_NAME).write_text(json.dumps(gltf, indent=2), encoding='utf-8')
    scene = write_scene_file(SCENE_DIR / GLTF_NAME)

    lines = ['ui hide']
    for name, view in CAPTURES.items():
        lines += [f'settings set Ray Tracing View: View=Debug: {view}', f'frames {SETTLE_FRAMES}',
                  f'capture overlay {capture_path(name).with_suffix("")}']
    (SCENE_DIR / 'capture.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    print(f'scene file: {scene}')
    print(f'capture commands: {SCENE_DIR / "capture.txt"}')
    return 0


def capture_path(name):
    return SCENE_DIR / f'{name.replace(" ", "-")}.png'


def decoded_normals(capture, group):
    return capture[group][:, :3].astype(np.float64) / 255.0 * 2.0 - 1.0


def normal_problems(capture, group, expected):
    normals = decoded_normals(capture, group)
    error = float(np.abs(normals - expected).max())
    summary = f'mean {np.round(normals.mean(axis=0), 4).tolist()}, expected {np.round(expected, 4).tolist()}, max error {error:.4f}'
    return summary, [] if error <= TOLERANCE else [f'max error {error:.4f} is more than {TOLERANCE}']


def command_judge(args):
    try:
        captures = {name: np.asarray(Image.open(capture_path(name)).convert('RGB')) for name in CAPTURES}
    except OSError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    shapes = {capture.shape for capture in captures.values()}
    if len(shapes) != 1:
        print(f'error: the captures differ in size: {shapes}', file=sys.stderr)
        return 1

    hit = np.logical_and.reduce([capture.any(axis=2) for capture in captures.values()])
    groups = column_groups(eroded(hit, EDGE_PIXELS))
    sizes = [int(group.sum()) for group in groups]
    print(f'captures: {SCENE_DIR}')
    print(f'quads found: {len(groups)} groups of {sizes} pixels after trimming {EDGE_PIXELS} edge pixels')
    if len(groups) != len(QUADS) or min(sizes, default=0) < MIN_PIXELS:
        print(f'FAIL: expected {len(QUADS)} groups of at least {MIN_PIXELS} pixels')
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
    print('FAIL' if failed else 'PASS')
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('make', help=f'write the test scene and the capture commands to {SCENE_DIR}').set_defaults(handler=command_make)
    commands.add_parser('judge', help=f'judge the captures in {SCENE_DIR}').set_defaults(handler=command_judge)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
