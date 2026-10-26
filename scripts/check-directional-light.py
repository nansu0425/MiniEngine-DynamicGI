"""directional light 가 KHR_lights_punctual 의 정의대로 raster 에 들어가는지 판정한다.

  python scripts/check-directional-light.py check   # 아래 기준을 판정한다

번호는 작업 7 의 완료 기준의 번호다. 이 script 밖에서 보는 기준:
  [L2] light set 이 값을 읽은 대로 바꾸고 받을 수 없는 값은 거절한다, [L3] position 은 화면을 바꾸지 않는다, [L5] describe 줄의 restore,
  [G1] rotate gizmo 의 display: viewer.py selftest 의 check_light 와 editor 확인
  [G2] rotate gizmo 를 끌어 돌리는 것: 사용자가 본다
  [K1] raster 화면: viewer.py snapshot compare
  [K4] 다른 check script, [K5] viewer.py selftest, [K6] Debug, Profile, Release build, [K7] 문서

app 을 Debug 로 Sponza 에서 한 번, test scene 에서 한 번 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1
이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다.

light:
  [L1] state light 의 direction 이 rotation (quaternion) 으로 돌린 +Z 와 1e-6 안에서 같고, editor.ini 에 남는 Direction 과 float 로 같다.
  [L4] Sponza 에서 바꾼 light 를 끝낼 때 editor.ini 에 남고, 그 값을 test scene 의 실행에 settings set 으로 넣으면 state light 가 모든
       값에서 같다. settings set 은 app 이 시작할 때 editor.ini 를 읽는 것과 같은 load 를 부른다. 그 editor.ini 의 Engine Tuning 에
       Viewer/Lighting 의 예전 light 항목이 없다.

raster (test scene): quad 는 Front Face view 의 앞면 pixel 을 가장자리 EDGE_PIXELS 깎은 것이다.
  [P5] (raster) color COLOR 와 intensity 2 배의 raster scene capture 가 흰색, 원래 intensity 의 capture 의 channel 별 배수와 앞면 pixel
       에서 bit 까지 같다. 2 의 거듭제곱 배는 float 의 반올림 없이 곱해진다.
  [R1] intensity 0 이면 raster 의 앞면 pixel 이 모두 0 이다. test scene 에는 emission 이 없다.

그대로인 것 (Sponza): before 는 작업 7 전 (a9423e3) 에 같은 조건으로 찍어 BEFORE_DIR 에 둔 capture 다 (BEFORE_DIR/manifest.json).
  [K2] 기본 light 에서 Light Visibility, Light Culling Difference 의 overlay 가 before 의 Sun Visibility, Sun Culling Difference 와 같다.

source:
  [K8] tracked 와 untracked (ignore 하지 않은) 파일에서 sun 의 세 표기가 SUN_EXCEPTIONS 밖에 없다.
"""

import argparse
import configparser
import json
import math
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from gltfwriter import GLTF_FLOAT, GLTF_UNSIGNED_SHORT, BufferBuilder, flatten
from imagefiles import ImageFileError, read_dds_rgba32f
from imagemasks import column_groups, eroded
from viewer import HARNESS_DIR, REPO_ROOT, SETTLE_FRAMES, VIEWER_DIR, HarnessError, Session, capture, start_instance, stop_instance

SCENE_DIR = HARNESS_DIR / 'directional-light'
SCENE_NAME = 'DirectionalLight'
KEPT_DIR = HARNESS_DIR / 'kept' / 'directional-light'
BEFORE_DIR = KEPT_DIR / 'before'
LIGHT_SECTION = 'Directional Light'

# light 는 위 앞쪽에서 온다. 앞면이 +z 인 quad 와의 cos 는 0.6 이다.
LIGHT_DIRECTION = (0.0, 0.8, 0.6)
INTENSITY = float(np.float32(math.pi))
ALBEDO = 0.5
COLOR = (1.0, 0.5, 0.25)
EDGE_PIXELS = 2
MIN_PIXELS = 300
FRONT_FACE_COLOR = (0, 255, 0)

QUAD_POSITIONS = [(-1.0, 1.0, 0.0), (1.0, 1.0, 0.0), (1.0, -1.0, 0.0), (-1.0, -1.0, 0.0)]
QUAD_UV = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
# glTF 의 앞면은 counter-clockwise 다. +z 에서 보면 두 triangle 모두 counter-clockwise 다.
QUAD_INDICES = [0, 3, 2, 0, 2, 1]
# x 축으로 θ 돌려 +z 가 (0, -sin θ, cos θ) = (0, -0.8, 0.6) 이 되는 rotation. light 를 등지고 camera 는 앞면을 본다.
BACK_ROTATION = (math.sin(math.atan2(0.8, 0.6) / 2), 0.0, 0.0, math.cos(math.atan2(0.8, 0.6) / 2))
# occluder 는 receiver 에서 light 쪽으로 OCCLUDER_DISTANCE 떨어져 그림자가 receiver 가운데에 진다. camera 에서는 그림자보다 위에 보인다.
OCCLUDER_DISTANCE = 1.5
OCCLUDER_SCALE = 0.4

# 왼쪽부터 column 덩어리 하나씩이다. normal 은 vertex NORMAL (object space), geometric 은 그 quad 의 world 의 geometric normal 이다.
QUADS = [
    SimpleNamespace(name='lit', x=-7.5, normal=(0.0, 0.0, 1.0), rotation=(0.0, 0.0, 0.0, 1.0), occluder=None),
    SimpleNamespace(name='tilted', x=-4.5, normal=(0.6, 0.0, 0.8), rotation=(0.0, 0.0, 0.0, 1.0), occluder=None),
    SimpleNamespace(name='away', x=-1.5, normal=(0.0, -0.8, 0.6), rotation=(0.0, 0.0, 0.0, 1.0), occluder=None),
    SimpleNamespace(name='back', x=1.5, normal=(0.0, 0.0, 1.0), rotation=BACK_ROTATION, occluder=None),
    SimpleNamespace(name='single', x=4.5, normal=(0.0, 0.0, 1.0), rotation=(0.0, 0.0, 0.0, 1.0), occluder='single-sided'),
    SimpleNamespace(name='double', x=7.5, normal=(0.0, 0.0, 1.0), rotation=(0.0, 0.0, 0.0, 1.0), occluder='doubleSided'),
]

BEFORE_VIEWS = {'Debug: Sun Visibility': 'Debug: Light Visibility', 'Debug: Sun Culling Difference': 'Debug: Light Culling Difference'}
# 이 light 의 이름이 sun 이던 때의 기록과, 이 script 의 판정 자체
SUN_EXCEPTIONS = [
    ('docs/reference-path-tracer-roadmap.md', '7. Directional Light: sun 을'),
    ('docs/harness-roadmap.md', 'agent 가 sun 의 translate gizmo'),
]
SUN_EXCLUDED_PATHS = [':!*.pdf', ':!MiniEngine/Model/Loader/json.hpp', ':!MiniEngine/Model/Sponza', ':!scripts/check-directional-light.py']


def write_test_scene():
    builder = BufferBuilder()
    vertex_count = len(QUAD_POSITIONS)
    position = builder.add(flatten(QUAD_POSITIONS), GLTF_FLOAT, 'VEC3', vertex_count, with_bounds=True)
    uv = builder.add(flatten(QUAD_UV), GLTF_FLOAT, 'VEC2', vertex_count)
    indices = builder.add(QUAD_INDICES, GLTF_UNSIGNED_SHORT, 'SCALAR', len(QUAD_INDICES))
    flat = builder.add(flatten([(0.0, 0.0, 1.0)] * vertex_count), GLTF_FLOAT, 'VEC3', vertex_count)

    def material(name, double_sided):
        return {'name': name, 'doubleSided': double_sided,
                'pbrMetallicRoughness': {'baseColorFactor': [ALBEDO, ALBEDO, ALBEDO, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 1.0}}

    materials = [material('single-sided', False), material('doubleSided', True)]
    meshes, nodes = [], []
    for quad in QUADS:
        normal = flat if quad.normal == (0.0, 0.0, 1.0) else builder.add(flatten([quad.normal] * vertex_count), GLTF_FLOAT, 'VEC3', vertex_count)
        meshes.append({'name': quad.name, 'primitives': [{'attributes': {'POSITION': position, 'NORMAL': normal, 'TEXCOORD_0': uv},
                                                          'indices': indices, 'material': 0}]})
        nodes.append({'name': quad.name, 'mesh': len(meshes) - 1, 'translation': [quad.x, 0.0, 0.0], 'rotation': list(quad.rotation)})
        if quad.occluder:
            meshes.append({'name': f'{quad.name}-occluder', 'primitives': [{
                'attributes': {'POSITION': position, 'NORMAL': flat, 'TEXCOORD_0': uv}, 'indices': indices,
                'material': 0 if quad.occluder == 'single-sided' else 1}]})
            offset = np.array(LIGHT_DIRECTION) * OCCLUDER_DISTANCE
            nodes.append({'name': f'{quad.name}-occluder', 'mesh': len(meshes) - 1, 'translation': [quad.x + offset[0], offset[1], offset[2]],
                          'scale': [OCCLUDER_SCALE] * 3})

    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/check-directional-light.py'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'buffers': [{'uri': f'{SCENE_NAME}.bin', 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    (SCENE_DIR / f'{SCENE_NAME}.bin').write_bytes(builder.data)
    (SCENE_DIR / f'{SCENE_NAME}.gltf').write_text(json.dumps(gltf, indent=2), encoding='utf-8')


def overlay(session, view, name):
    session.ask_ok(f'settings set Ray Tracing View: View={view}')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    image = np.asarray(Image.open(capture(session, 'overlay', name)).convert('RGB'))
    session.ask_ok('settings set Ray Tracing View: View=Off')
    return image


def light_state(session):
    return session.ask_ok('state light')['state']['light']


def vector_of(fields, axes='xyz'):
    return np.array([float(fields[axis]) for axis in axes])


def rotate(quaternion, vector):
    axis, w = quaternion[:3], quaternion[3]
    twice = 2.0 * np.cross(axis, vector)
    return vector + w * twice + np.cross(axis, twice)


def set_light(session, values):
    """editor.ini 의 key 와 value 로 light 를 정한다."""
    session.ask_ok(f'settings set {LIGHT_SECTION}: ' + '; '.join(f'{key}={value}' for key, value in values.items()))


def same_bits(a, b):
    return np.array_equal(np.asarray(a, dtype=np.float32).view(np.uint32), np.asarray(b, dtype=np.float32).view(np.uint32))


# --- Sponza ---------------------------------------------------------------------------------

def check_saved_direction(session, check):
    light = light_state(session)
    saved = session.ask_ok(f'settings get {LIGHT_SECTION}')['values']
    direction = vector_of(light['direction'])
    rotated = rotate(vector_of(light['rotation'], 'xyzw'), np.array([0.0, 0.0, 1.0]))
    in_ini = np.array([float(value) for value in saved['Direction'].split(',')])
    check('[L1] state light direction is the +Z of its rotation and the Direction editor.ini keeps',
          np.abs(rotated - direction).max() <= 1e-6 and same_bits(in_ini, direction),
          f'rotation {light["rotation"]}, direction {light["direction"]}, saved {saved["Direction"]}')


def check_before(session, check):
    manifest = json.loads((BEFORE_DIR / 'manifest.json').read_text(encoding='utf-8'))
    differing = []
    for name, entry in manifest['files'].items():
        if 'view' not in entry:
            continue
        session.ask_ok(f'pose {entry["pose"]}')
        now = overlay(session, BEFORE_VIEWS[entry['view']], f'after-{name}')
        before = np.asarray(Image.open(BEFORE_DIR / name).convert('RGB'))
        if not np.array_equal(now, before):
            differing.append(f'{name}: {int(np.count_nonzero(np.any(now != before, axis=2)))} pixels')
    check(f'[K2] the light visibility views are as before ({manifest["code"]})', not differing, '; '.join(differing))


def run_sponza(build_first, check):
    """Sponza 의 판정을 하고, 끝낼 때 editor.ini 에 남은 light 와 그때의 state light 를 돌려준다."""
    instance = start_instance('Debug', build_first=build_first)
    stored = None
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_saved_direction(session, check)
            check_before(session, check)
            session.ask_ok('light set direction=0.2,0.9,-0.3 color=0.75,0.5,0.25 intensity=2.5 position=10,20,30')
            stored = light_state(session)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('Sponza ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
        return None
    ini = configparser.ConfigParser(delimiters=('=',), comment_prefixes=(';',), interpolation=None)
    ini.optionxform = str
    ini.read(Path(instance['run_dir']) / 'editor.ini', encoding='utf-8')
    return SimpleNamespace(state=stored, saved=dict(ini[LIGHT_SECTION]) if ini.has_section(LIGHT_SECTION) else {},
                           tuning=dict(ini['Engine Tuning']) if ini.has_section('Engine Tuning') else {})


# --- test scene -----------------------------------------------------------------------------

def check_stored(session, check, stored):
    if stored is None:
        return
    old = [key for key in stored.tuning if key.startswith('Viewer/Lighting/')]
    if not stored.saved:
        check('[L4] editor.ini keeps the light', False, f'no [{LIGHT_SECTION}] section')
        return
    set_light(session, stored.saved)
    loaded = light_state(session)
    check('[L4] the light saved in editor.ini loads back as it was, and Engine Tuning keeps no light of its own',
          loaded == stored.state and not old, f'{stored.state} -> {loaded}; Engine Tuning {old}')


def judge_raster(check, front_face, raster):
    front = np.all(front_face == FRONT_FACE_COLOR, axis=2)
    groups = column_groups(eroded(front, EDGE_PIXELS))
    sizes = [int(group.sum()) for group in groups]
    if not check(f'the test scene shows {len(QUADS)} quads of at least {MIN_PIXELS} pixels', len(groups) == len(QUADS) and min(sizes, default=0) >= MIN_PIXELS,
                 f'{len(groups)} groups of {sizes} pixels after trimming {EDGE_PIXELS} edge pixels'):
        return
    color = np.array(COLOR, dtype=np.float32)
    mask = eroded(front, EDGE_PIXELS)
    colored = same_bits(raster.colored[mask], raster.white[mask] * color)
    doubled = same_bits(raster.doubled[mask], raster.white[mask] * np.float32(2.0))
    check(f'[P5] (raster) the raster scales by color {COLOR} and by twice the intensity to the bit', colored and doubled,
          f'color {colored}, intensity {doubled}')
    check('[R1] the raster draws 0 on every front pixel with intensity 0', not np.any(raster.dark[mask]),
          f'{int(np.count_nonzero(np.any(raster.dark[mask] != 0, axis=1)))} pixels are not 0')


def scene_rgb(session, name):
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    return read_dds_rgba32f(capture(session, 'scene', name))[..., :3]


def run_test_scene(build_first, check, stored):
    instance = start_instance('Debug', model=f'{SCENE_DIR.relative_to(VIEWER_DIR).as_posix()}/{SCENE_NAME}.gltf', build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_stored(session, check, stored)
            direction = ','.join(f'{value:g}' for value in LIGHT_DIRECTION)
            session.ask_ok(f'light set direction={direction} color=1,1,1 intensity={INTENSITY!r} position=0,0,0')

            raster = SimpleNamespace(white=scene_rgb(session, 'raster-white'))
            session.ask_ok(f'light set color={",".join(str(value) for value in COLOR)}')
            raster.colored = scene_rgb(session, 'raster-colored')
            session.ask_ok(f'light set color=1,1,1 intensity={2 * INTENSITY!r}')
            raster.doubled = scene_rgb(session, 'raster-doubled')
            session.ask_ok('light set intensity=0')
            raster.dark = scene_rgb(session, 'raster-dark')
            session.ask_ok(f'light set intensity={INTENSITY!r}')

            front_face = overlay(session, 'Debug: Front Face', 'front-face')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('the test scene ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
        return
    judge_raster(check, front_face, raster)


# --- source ---------------------------------------------------------------------------------

def check_sun_name(check):
    found = subprocess.run(['git', '-c', 'core.quotepath=false', 'grep', '--untracked', '-n', '-E', 'sun|Sun|SUN', '--', '.', *SUN_EXCLUDED_PATHS],
                           cwd=REPO_ROOT, capture_output=True, text=True, encoding='utf-8').stdout.splitlines()
    left = [line for line in found if not any(line.startswith(path + ':') and text in line for path, text in SUN_EXCEPTIONS)]
    check('[K8] the name sun is left only in the exceptions', not left, '; '.join(line[:160] for line in left))


def command_check(args):
    SCENE_DIR.mkdir(parents=True, exist_ok=True)
    write_test_scene()
    check = Checks()
    stored = run_sponza(not args.no_build, check)
    run_test_scene(False, check, stored)
    check_sun_name(check)
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    check_parser = commands.add_parser('check', help='judge the criteria above with the Debug app')
    check_parser.add_argument('--no-build', action='store_true', help='run the Debug app as it is built')
    check_parser.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
