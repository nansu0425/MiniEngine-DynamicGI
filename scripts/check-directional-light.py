"""directional light 가 KHR_lights_punctual 의 정의대로 raster 와 PT 에 들어가는지 판정한다.

  python scripts/check-directional-light.py check   # 아래 기준을 판정한다

번호는 작업 7 의 완료 기준의 번호다. 이 script 밖에서 보는 기준:
  [L2] light set 이 값을 읽은 대로 바꾸고 받을 수 없는 값은 거절한다, [L3] position 은 화면을 바꾸지 않는다, [L5] describe 줄의 restore,
  [L6] light reset (panel 의 Reset Transform) 이 transform 만 회전 없는 원점으로 둔다, [G1] rotate gizmo 의 display,
  [G3] editor axes (gizmo toolbar 의 Local, World) 가 rotate ring 의 축을 local 과 world 로 바꾼다: viewer.py selftest 의
  check_light 와 editor 확인
  [G2] rotate gizmo 를 끌어 돌리는 것, gizmo toolbar 의 Local, World 와 Details panel 의 Reset Transform 을 누르는 것: 사용자가 본다

app 을 Debug 로 Sponza 에서 한 번, test scene 에서 한 번 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1
이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다. PT 의 값은 capture pt
의 float export 로 본다.

light:
  [L1] state light 의 direction 이 rotation (quaternion) 으로 돌린 +Z 와 1e-6 안에서 같고, editor get 의 Direction (scene save 가
       scene file 에 쓰는 값) 과 float 로 같다.
  [L4] Sponza 에서 바꾼 light 를 editor get 으로 읽어 test scene 의 실행에 editor set 으로 넣으면 state light 가 모든 값에서 같다.
       editor set 은 scene file 을 열 때와 같은 LoadSettings 를 부른다. Sponza 를 끝낸 run 의 user.ini 의 Engine Tuning 에
       Viewer/Lighting 의 예전 light 항목이 없다. scene file 에 쓰고 다시 읽는 것은 check-scene-open.py 의 [O11] 이 본다.

PT 의 직접광 (test scene): Max Bounces 1, Sky Light 0, Russian roulette off 다. sky 와 emission 이 0 이라 path 의 값은 처음 맞은 곳에서
반사한 light 뿐이고 그 값은 pixel 안의 어디에 맞아도 같으므로 분산이 0 이다. quad 는 Front Face view 의 앞면 pixel 을 가장자리 EDGE_PIXELS
깎아 column 이 이어진 덩어리로 나누고, 덩어리 안을 Light Visibility view 의 흰색 (보임) 과 검정 (가려짐) 으로 나눈다.
  [P1] Geometric Normals 를 켜면 보이는 pixel 이 모두 albedo x color x intensity x cos / π 와 상대 PT_TOLERANCE 안에서 같다. cos 는
       light 와 geometric normal 의 cos 이고, albedo 0.5, intensity 는 float 의 π 라서 cos 가 1 이면 0.5 cd/m² 다. geometric normal 은
       float 의 vertex 위치로 구하므로 식과 float 오차만큼만 다르다.
  [P2] Max Bounces 0 이면 모든 pixel 이 0 이다. light 를 등진 quad (back) 는 Light Visibility 가 모두 검정이고 PT 도 0 이다.
  [P3] 가려진 pixel 은 0 이다. single-sided occluder 는 그 뒷면이 receiver 를 향하므로 docs/policy.md 의 "camera 밖에서 본 doubleSided" 대로
       그림자를 만들지 않고 (single 의 검정 pixel 0 개), doubleSided occluder 는 MIN_SHADOW_PIXELS 이상의 그림자를 만든다.
  [P4] Geometric Normals 를 끄면 보이는 pixel 의 cos 가 vertex NORMAL 로 구한 cos 와 SHADING_COSINE_TOLERANCE 안에서 같다. shading normal
       이 geometric normal 과 다른 quad (tilted, away) 에서 두 cos 는 그보다 훨씬 다르고, away 는 shading normal 이 light 를 등져 0 이다.
       허용 오차는 PT 가 받는 shading normal 의 오차다. normal map 이 없는 material 은 vertex normal 을 그대로 쓰고 (HitSurface.hlsli),
       vertex normal 은 R10G10B10A2 로 저장해 성분마다 1/1023 까지 다르다.
  [P5] color COLOR 와 intensity 2 배의 PT export 가 흰색, 원래 intensity 의 export 의 channel 별 배수와 rgb 의 bit 까지 같다. raster scene
       capture 도 앞면 pixel 에서 그렇다. 2 의 거듭제곱 배는 float 의 반올림 없이 곱해진다.
  [R1] intensity 0 이면 raster 의 앞면 pixel 이 모두 0 이다. test scene 에는 emission 이 없다.

PT 의 직접광 (Sponza): harness baseline 의 시점, Russian roulette off, Sky Light 0, PT_SAMPLES spp 다. 판정은 check-gltf-brdf.py 도
쓰도록 directlightchecks.py 에 있고, 아래의 대문자 이름도 거기 있다.
  [P6] Max Bounces 1 과 Geometric Normals on 으로 SPONZA_LIGHTS 의 방향마다 쌓는다. Light Visibility 의 검정을 깎은 pixel 에서 PT 가 0 이
       아닌 것 (가려졌는데 밝음) 과, 흰색을 깎은 pixel 에서 Base Color 가 0 이 아니고 Metallic 이 1 이 아닌데 (Lambert 의 albedo 가 있는데)
       PT 가 0 인 것 (보이는데 어두움) 이 각각 그 region 의 MISMATCH_FRACTION 이하이고, 4 방향으로 이어진 가장 큰 덩어리가
       MAX_MISMATCH_CLUSTER pixel 이하다. 깎은 흰색과 검정이 각각 MIN_REGION_PIXELS 이상이고 NaN 이나 Inf 인 sample 이 없다.
       Light Visibility 는 pixel 가운데의 ray 하나로, PT 는 pixel 안을 고르게 쏜 ray 로 정하므로 sub-pixel 의 틈에서 갈린다. 작업 7 을
       판정할 때 그런 pixel 은 alpha test 하는 잎과 그 잎의 그림자에 흩어져 있었다 (pick 으로 66 개 중 47 개가 alpha test). 그림자 ray 의
       출발점이나 culling 이 틀리면 면을 따라 이어진 덩어리로 어긋난다.
  [P7] Geometric Normals off, 같은 Seed 로 Max Bounces 를 1, 2, 4 로 올리면 어떤 pixel 도 PT_SAMPLES ulp 보다 더 어두워지지 않고 평균은
       매번 커진다. light 는 RNG 를 쓰지 않으므로 path 가 앞 run 의 path 를 그대로 이어 가며, x2 부터 반사한 light 와 반사 횟수의
       off-by-one 을 본다. 누적의 running mean 은 float 로 sample 마다 반올림하므로, sample 이 모두 같거나 밝아도 평균의 마지막 bit 는
       sample 수만큼까지 어두워질 수 있다. 작업 7 을 판정할 때 그런 pixel 은 1 ~ 2 ulp 였다.
  [P8] PT 를 쌓는 동안 light 의 direction, color, intensity 를 바꾸면 pt 의 resets 가 1 늘고, position 을 바꾸면 늘지 않는다.

source:
  [K8] tracked 와 untracked (ignore 하지 않은) 파일에서 sun 의 세 표기가 SUN_EXCEPTIONS 밖에 없다.

test scene 의 PT 와 Sponza 의 직접광은 발표에 쓰도록 KEPT_DIR 에 DDS 와 보기용 PNG 로 남긴다.
"""

import argparse
import configparser
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from gltfwriter import GLTF_FLOAT, GLTF_UNSIGNED_SHORT, BufferBuilder, flatten
from imagefiles import ImageFileError, read_dds_rgba32f
from directlightchecks import (DIRECT, EDGE_PIXELS, LIGHT_OBJECT, WAIT_TIMEOUT, accumulate_export, check_bounces, check_sponza_direct,
                               overlay, set_light)
from imagemasks import column_groups, eroded
from pathtracerchecks import set_path_tracer
from viewer import (HARNESS_DIR, PREVIEW_EXPOSURE, REPO_ROOT, SETTLE_FRAMES, HarnessError, Session, capture, make_view,
                    start_instance, stop_instance, write_scene_file)

SCENE_DIR = HARNESS_DIR / 'directional-light'
SCENE_NAME = 'DirectionalLight'
KEPT_DIR = HARNESS_DIR / 'kept' / 'directional-light'

# light 는 위 앞쪽에서 온다. 앞면이 +z 인 quad 와의 cos 는 0.6 이다.
LIGHT_DIRECTION = (0.0, 0.8, 0.6)
INTENSITY = float(np.float32(math.pi))
ALBEDO = 0.5
COLOR = (1.0, 0.5, 0.25)
PT_TOLERANCE = 1e-5
# R10G10B10A2 normal 의 성분마다 1/1023 인 |Δn| 의 상한 0.0017 보다 조금 크게 잡았다.
SHADING_COSINE_TOLERANCE = 0.002
MIN_PIXELS = 300
MIN_SHADOW_PIXELS = 300
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

# 이 light 의 이름이 sun 이던 때의 기록. 이 script 의 판정 자체는 SUN_EXCLUDED_PATHS 가 뺀다.
SUN_EXCEPTIONS = [
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


def keep(path, target):
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    kept = KEPT_DIR / path.name
    shutil.copy2(path, kept)
    make_view(kept, target, kept.with_suffix('.png'), None, 1 << 16, PREVIEW_EXPOSURE)


def accumulate(session, settings, name, keep_it=False):
    """settings 로 처음부터 쌓은 PT export 의 rgba 와, NaN 이나 Inf 라서 버린 sample 수."""
    rgba, invalid, path = accumulate_export(session, settings, name)
    if keep_it:
        keep(path, 'pt')
    return rgba, invalid


def light_state(session):
    return session.ask_ok('state light')['state']['light']


def vector_of(fields, axes='xyz'):
    return np.array([float(fields[axis]) for axis in axes])


def rotate(quaternion, vector):
    axis, w = quaternion[:3], quaternion[3]
    twice = 2.0 * np.cross(axis, vector)
    return vector + w * twice + np.cross(axis, twice)


def same_bits(a, b):
    return np.array_equal(np.asarray(a, dtype=np.float32).view(np.uint32), np.asarray(b, dtype=np.float32).view(np.uint32))


# --- Sponza ---------------------------------------------------------------------------------

def check_saved_direction(session, check):
    light = light_state(session)
    saved = session.ask_ok(f'editor get {LIGHT_OBJECT}')['values']
    direction = vector_of(light['direction'])
    rotated = rotate(vector_of(light['rotation'], 'xyzw'), np.array([0.0, 0.0, 1.0]))
    in_settings = np.array([float(value) for value in saved['Direction'].split(',')])
    check('[L1] state light direction is the +Z of its rotation and the Direction editor get gives',
          np.abs(rotated - direction).max() <= 1e-6 and same_bits(in_settings, direction),
          f'rotation {light["rotation"]}, direction {light["direction"]}, saved {saved["Direction"]}')


def reflects_nothing(base_color, metallic):
    """Lambert 의 albedo baseColor x (1 - metallic) 가 0 인 pixel"""
    return np.all(base_color == 0, axis=2) | np.all(metallic == 255, axis=2)


def check_resets(session, check):
    set_path_tracer(session, DIRECT | {'Target Samples': 1 << 20})
    session.ask_ok('pt reset')
    session.ask_ok('pt wait samples=2', timeout=WAIT_TIMEOUT)
    counts = {}
    for name, request in [('direction', 'direction=0.3,0.8,0.5'), ('color', 'color=1,0.5,0.25'), ('intensity', 'intensity=2'),
                          ('position', 'position=10,20,30')]:
        before = session.ask_ok('state pt')['state']['pt']['resets']
        session.ask_ok(f'light set {request}')
        session.ask_ok('frames 2')
        counts[name] = session.ask_ok('state pt')['state']['pt']['resets'] - before
    check('[P8] changing the light direction, color or intensity starts the path tracer over, and its position does not',
          counts == {'direction': 1, 'color': 1, 'intensity': 1, 'position': 0}, str(counts))
    set_path_tracer(session, {'Show': 0})


def run_sponza(build_first, check):
    """Sponza 의 판정을 하고, 끝내기 전의 state light 와 editor get 의 light, 끝낸 run 의 user.ini 의 Engine Tuning 을 돌려준다."""
    instance = start_instance('Debug', build_first=build_first)
    stored, saved = None, {}
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            camera = session.ask_ok('state camera')['state']['camera']
            start_pose = ' '.join(str(camera[key]) for key in ['x', 'y', 'z', 'heading', 'pitch'])
            default_light = session.ask_ok(f'editor get {LIGHT_OBJECT}')['values']
            check_saved_direction(session, check)
            check_sponza_direct(session, check, start_pose, default_light, '[P6]', {}, reflects_nothing, lambda path: keep(path, 'pt'))
            check_bounces(session, check, start_pose, '[P7]', {})
            check_resets(session, check)
            session.ask_ok('light set direction=0.2,0.9,-0.3 color=0.75,0.5,0.25 intensity=2.5 position=10,20,30')
            stored = light_state(session)
            saved = session.ask_ok(f'editor get {LIGHT_OBJECT}')['values']
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('Sponza ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
        return None
    ini = configparser.ConfigParser(delimiters=('=',), comment_prefixes=(';',), interpolation=None)
    ini.optionxform = str
    ini.read(Path(instance['run_dir']) / 'user.ini', encoding='utf-8')
    return SimpleNamespace(state=stored, saved=saved, tuning=dict(ini['Engine Tuning']) if ini.has_section('Engine Tuning') else {})


# --- test scene -----------------------------------------------------------------------------

def check_stored(session, check, stored):
    if stored is None:
        return
    old = [key for key in stored.tuning if key.startswith('Viewer/Lighting/')]
    if not stored.saved:
        check('[L4] editor get gives the light', False, f'no values of {LIGHT_OBJECT}')
        return
    set_light(session, stored.saved)
    loaded = light_state(session)
    check('[L4] the light editor get gave loads back as it was with editor set, and Engine Tuning keeps no light of its own',
          loaded == stored.state and not old, f'{stored.state} -> {loaded}; Engine Tuning {old}')


def judge_quads(check, front_face, visibility, images, raster):
    front = np.all(front_face == FRONT_FACE_COLOR, axis=2)
    groups = column_groups(eroded(front, EDGE_PIXELS))
    sizes = [int(group.sum()) for group in groups]
    if not check(f'the test scene shows {len(QUADS)} quads of at least {MIN_PIXELS} pixels', len(groups) == len(QUADS) and min(sizes, default=0) >= MIN_PIXELS,
                 f'{len(groups)} groups of {sizes} pixels after trimming {EDGE_PIXELS} edge pixels'):
        return
    light = np.array(images.light_direction)
    visible = eroded(np.all(visibility == 255, axis=2), EDGE_PIXELS)
    occluded = eroded(np.all(visibility == 0, axis=2), EDGE_PIXELS)

    def expected(cosine):
        return ALBEDO * INTENSITY * max(cosine, 0.0) / math.pi

    geometric_wrong, shading_wrong, shadows = [], [], {}
    for quad, group in zip(QUADS, groups):
        geometric = rotate(np.array(quad.rotation), np.array([0.0, 0.0, 1.0]))
        shading = rotate(np.array(quad.rotation), np.array(quad.normal) / np.linalg.norm(quad.normal))
        lit = images.geometric[group & visible][:, :3]
        value = expected(float(geometric @ light))
        if lit.size and (np.any(lit != 0.0) if value == 0.0 else np.abs(lit / value - 1.0).max() > PT_TOLERANCE):
            geometric_wrong.append(f'{quad.name}: visible {lit.min():.9g} ~ {lit.max():.9g}, expected {value:.9g}')
        lit = images.shading[group & visible][:, :3]
        value = expected(float(shading @ light))
        if lit.size and np.abs(lit - value).max() > expected(SHADING_COSINE_TOLERANCE):
            shading_wrong.append(f'{quad.name}: visible {lit.min():.9g} ~ {lit.max():.9g}, expected {value:.9g}')
        for normals, image in [('shading', images.shading), ('geometric', images.geometric)]:
            dark = image[group & occluded][:, :3]
            if dark.size and np.any(dark != 0.0):
                geometric_wrong.append(f'{quad.name} {normals} normals: {int(np.count_nonzero(np.any(dark != 0.0, axis=1)))} occluded pixels are lit')
        shadows[quad.name] = int((group & occluded).sum())

    check('[P1] with Geometric Normals on every visible pixel is albedo x color x intensity x cos / pi of the geometric normal, '
          'and [P3] every occluded pixel is 0', not geometric_wrong, '; '.join(geometric_wrong))
    check(f'[P4] with Geometric Normals off the cos is of the vertex NORMAL within {SHADING_COSINE_TOLERANCE}', not shading_wrong,
          '; '.join(shading_wrong))
    back = groups[[quad.name for quad in QUADS].index('back')]
    check('[P2] Max Bounces 0 gives 0 everywhere, and the quad facing away from the light is occluded and 0',
          not np.any(images.no_bounce[..., :3]) and not (back & visible).any() and not np.any(images.shading[back][:, :3]),
          f'{int(np.count_nonzero(np.any(images.no_bounce[..., :3] != 0, axis=2)))} pixels are not 0 with Max Bounces 0')
    check('[P3] a single-sided occluder seen from behind casts no shadow, and a doubleSided one does',
          shadows['single'] == 0 and shadows['double'] >= MIN_SHADOW_PIXELS, f'shadow pixels {shadows}')

    color = np.array(COLOR, dtype=np.float32)
    pt_colored = same_bits(images.colored[..., :3], images.shading[..., :3] * color)
    pt_doubled = same_bits(images.doubled[..., :3], images.shading[..., :3] * np.float32(2.0))
    mask = eroded(front, EDGE_PIXELS)
    raster_colored = same_bits(raster.colored[mask], raster.white[mask] * color)
    raster_doubled = same_bits(raster.doubled[mask], raster.white[mask] * np.float32(2.0))
    check(f'[P5] the path tracer and the raster scale by color {COLOR} and by twice the intensity to the bit',
          pt_colored and pt_doubled and raster_colored and raster_doubled,
          f'path tracer color {pt_colored}, intensity {pt_doubled}; raster color {raster_colored}, intensity {raster_doubled}')
    check('[R1] the raster draws 0 on every front pixel with intensity 0', not np.any(raster.dark[mask]),
          f'{int(np.count_nonzero(np.any(raster.dark[mask] != 0, axis=1)))} pixels are not 0')


def scene_rgb(session, name):
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    return read_dds_rgba32f(capture(session, 'scene', name))[..., :3]


def run_test_scene(build_first, check, stored):
    instance = start_instance('Debug', scene=write_scene_file(SCENE_DIR / f'{SCENE_NAME}.gltf'), build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_stored(session, check, stored)
            direction = ','.join(f'{value:g}' for value in LIGHT_DIRECTION)
            session.ask_ok(f'light set direction={direction} color=1,1,1 intensity={INTENSITY!r} position=0,0,0')
            light_direction = vector_of(light_state(session)['direction'])

            raster = SimpleNamespace(white=scene_rgb(session, 'raster-white'))
            session.ask_ok(f'light set color={",".join(str(value) for value in COLOR)}')
            raster.colored = scene_rgb(session, 'raster-colored')
            session.ask_ok(f'light set color=1,1,1 intensity={2 * INTENSITY!r}')
            raster.doubled = scene_rgb(session, 'raster-doubled')
            session.ask_ok('light set intensity=0')
            raster.dark = scene_rgb(session, 'raster-dark')
            session.ask_ok(f'light set intensity={INTENSITY!r}')

            front_face = overlay(session, 'Debug: Front Face', 'front-face')
            visibility = overlay(session, 'Debug: Light Visibility', 'light-visibility')
            images = SimpleNamespace(light_direction=light_direction)
            images.shading = accumulate(session, DIRECT, 'pt-shading', keep_it=True)[0]
            images.geometric = accumulate(session, DIRECT | {'Geometric Normals': 1}, 'pt-geometric')[0]
            images.no_bounce = accumulate(session, DIRECT | {'Max Bounces': 0}, 'pt-no-bounce')[0]
            session.ask_ok(f'light set color={",".join(str(value) for value in COLOR)}')
            images.colored = accumulate(session, DIRECT, 'pt-colored')[0]
            session.ask_ok(f'light set color=1,1,1 intensity={2 * INTENSITY!r}')
            images.doubled = accumulate(session, DIRECT, 'pt-doubled')[0]
            set_path_tracer(session, {'Show': 0})
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('the test scene ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
        return
    judge_quads(check, front_face, visibility, images, raster)


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
    print(f'kept captures: {KEPT_DIR}')
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
