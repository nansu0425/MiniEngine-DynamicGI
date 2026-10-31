"""PathTracer 의 Shading DDGI Probe Hit 가 DDGI 논문의 probe ray hit shading 을 따르는지 판정한다.

  python scripts/check-probe-hit-shading.py check   # Debug 로 띄워 아래 기준을 판정한다

번호는 Shading DDGI Probe Hit 를 더한 commit (d84b6c3) 의 완료 기준의 번호다. probe hit 의 shading 과 그 출처는 SHADING_FILE 에, Shading 의 정의는 PathTracer.h 의 Shading 에
있다. check 는 app 을 test scene 둘에서 한 번씩, Sponza 에서 두 번 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도
실패하면 exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다.
PT 의 값은 capture pt 의 export 로 본다. export 의 rgb 는 sample 의 평균이고 a 는 더한 sample 수다. 이 script 밖에서 보는 기준:
  [E13] 정의: PathTracer.h 의 Output 과 Shading, PathTracerCS.hlsl 의 LAMBERT_SHADING, panel tooltip 의 글은 사람이 읽는다.

test scene (SCENE_DIR): scene file 의 camera 는 model 중심의 +z 쪽에서 -z 를 보며 model 을 화면에 담는다 (viewer.py 의 write_scene_file). light 는 direction LIGHT_DIRECTION, 흰색, intensity
float 의 π 라서 E⊥ / π 가 1 이다. PT 는 Output Radiance, Max Bounces 1, Sky Light 0, Russian roulette off, TEST_SAMPLES spp 다. 처음
맞은 곳에서 반사한 뒤에는 emission 과 sky 가 0 이므로 path 의 값은 처음 맞은 곳의 직접광뿐이다. 면의 pixel 은 Front Face view 의 앞면을
EDGE_PIXELS 깎고 column 으로 나눈 것이다. 비는 export / (E⊥ max(0, n·l) / π) 이고 n 은 quad 의 normal (0, 0, 1), l 은 state light 의
direction 이다.
  [E1] ALBEDO_SCENE 의 왼쪽 두 quad 는 texture 없는 baseColor factor BASE_COLOR 이고 metallic 이 0 과 1 이다. Geometric Normals 를 켠다.
       vertex normal 은 R10G10B10A2 로 담겨 성분마다 1/1023 까지 달라서 (로드맵의 뒤로 미룬 일) n·l 이 1e-3 넘게 어긋날 수 있기
       때문이다. Shading DDGI Probe Hit 의 비가 channel 마다 ρ' = (1 - metallic) (0.96 baseColor + 0.04) + metallic baseColor 와 상대
       오차의 중앙값이 ALBEDO_TOLERANCE 이하이고, pixel 의 ALBEDO_FRACTION 이상이 모든 channel 에서 ALBEDO_TOLERANCE 안이다. 벗어난 pixel
       의 수와 위치를 찍는다. Shading Lambert 의 비는 baseColor (1 - metallic) 와 같은 기준으로 맞고, ρ' 와는 상대 오차의 중앙값이
       ALBEDO_TOLERANCE 보다 큰 channel 이 있다. 두 식이 다르다는 것을 본다.
  [E2] 오른쪽 quad 는 CHECKER_SIZE 의 두 색 checker texture (CHECKER_COLORS, CHECKER_CELL texel 칸) 가 base color 이고 metallic 이 0 이다.
       조건은 [E1] 과 같다. Probe Hit 의 비는 channel 마다 변동 계수가 MIP_FLAT 이하이고, 평균이 app 이 만든 DDS 의 mip (SHADING_FILE 의
       kProbeHitTextureLod) texel 로 구한 ρ' 와 상대 MIP_TOLERANCE 안이다. 그 mip 은 1x1 이다. Lambert 의 비는 channel 마다 변동 계수가
       MIP_CONTRAST 이상이다.
  [E3] SHADOW_SCENE 은 floor quad 와, floor 를 향하는 single-sided occluder quad 다. camera 는 occluder 의 뒷면을 보므로 occluder 를
       cull 해서 floor 를 다 보고, floor 에서 light 쪽을 보면 occluder 의 앞면이 막는다 (docs/policy.md 의 "camera 밖에서 본 doubleSided").
       Geometric Normals 는 끈다. image 마다, floor pixel 의 channel 최댓값이 floor 의 LIT_PERCENTILE percentile 의 반을 넘으면 lit 이고
       아니면 unlit 이다. 비교하는 두 image 각각의 lit 과 unlit 의 경계에서 SHADOW_EDGE_PIXELS 이상 떨어진 floor pixel 에서, Probe Hit 와
       raster (harness baseline 의 scene capture) 의 lit/unlit 이 SHADOW_RASTER_AGREEMENT 이상, Probe Hit 와 Lambert (shadow ray) 의
       lit/unlit 이 SHADOW_LAMBERT_AGREEMENT 이상 같다. Probe Hit 의 unlit 은 MIN_UNLIT_PIXELS 이상이다.

Sponza (harness baseline 의 시점과 light):
  [E4] furnace: Geometric Normals on, Russian roulette off, Max Bounces FURNACE_BOUNCES, Sky S = FURNACE_SKY, Furnace Albedo a =
       FURNACE_ALBEDO, FURNACE_SAMPLES spp. k 는 SHADING_FILE 의 kProbeHitIndirectScale 이다. escape 하기 전에 n 번 맞은 path 의 값은
       e Σ_{i<n} (a k)^i + (a k)^n S 다. Furnace Emission e = S (1 - a k) 이면 이 값이 sample 마다 S 이므로, Radiance 의 모든 pixel 과
       Indirect Irradiance 의 hit pixel (a > 0) 의 E/π 가 S 와 상대 FURNACE_TOLERANCE 안이다. sample 마다 답이 정해지는 기존 furnace
       판정 (check-indirect-irradiance.py 의 [I2], check-path-tracer-export.py 의 [F2]) 과 같은 기준이다. e = S (1 - a) 로 둔 Radiance
       X_B 는 X_B = L (1 - X_C / S) + X_C 와 상대 FURNACE_TOLERANCE 안이다. L = S (1 - a) / (1 - a k) 이고, X_C 는 e = 0 인 run 이다.
       Russian roulette 를 끄면 RNG 를 쓰는 양이 albedo, emission, sky 에 따라 달라지지 않아 세 run 이 같은 path 를 그리므로
       (check-path-tracer-export.py), (a k)^n 의 pixel 평균이 X_C / S 다. k 가 1 이면 X_B 는 S 다. 그래서 X_B / S - 1 의 평균이 0 보다
       FURNACE_TOLERANCE 넘게 작아야 하고, 그 평균과 L / S - 1 (a = 0.9 이면 약 -2.2%) 을 찍는다.
  [E5] 첫 점: Indirect Irradiance, Max Bounces 1, light 와 Sky 는 그대로, Seed 0, FIRST_POINT_SAMPLES spp 로 FIRST_POINT_POSES 의 시점마다
       Probe Hit 와 Lambert 를 쌓은 export 가 a 는 bit 까지, rgb 는 LAMBERT_RUN_ULPS 안에서 같다. Lambert 는 같은 build 에서도 실행마다
       rgb 가 4 ulp 까지 다르다 (docs/reference-path-tracer-roadmap.md 의 뒤로 미룬 일). Sponza 에는 emission 이 없으므로 path 의 값은 첫 점에서 고른 방향의 sky 뿐이다. 첫 점의 normal map 을 다른 mip 으로
       읽으면 고르는 방향이 달라지고, 첫 점의 shading 을 더하면 값이 커지므로 둘 다 이 판정에 걸린다 (추론).
  [E10] RESTORE_LINE 의 조건 (Shading DDGI Probe Hit) 으로 쌓아 capture pt 로 export 한다. app 을 다시 띄워 그 export 의 줄을 restore
       하면 줄 (pt samples 밖) 과 state 의 camera, light, sky, settings.Engine Tuning (Path Tracer/Show 밖) 이 같고, Show 만 켜 같은 sample 수까지
       쌓은 export 의 rgba 가 bit 까지 같다.
  [E11] Probe Hit 로 쌓다가 light 방향을 바꾸면, 또 Shading 을 바꾸면 그때마다 pt 의 resets 가 1 늘고 samples 를 처음부터 센다.

source:
  [E6] PROBE_HIT_FUNCTIONS 가 SHADING_FILE 에만 정의되어 있고 PATH_TRACER_SHADER 가 모두 부른다. PATH_TRACER_SHADER 에서
       PROBE_HIT_SHADING 으로 고르는 구역에는 kPi 와 dot( 가 없고, 파일 전체에 FORBIDDEN_PATH_TRACER_TEXT 가 없다. 식과 상수를 PT 에
       따로 두지 않았다는 뜻이다. SHADING_FILE 에 FORBIDDEN_SHADING_PATTERN 이 없다. compute shader 도 부를 수 있다는 뜻이다.

GPU 의 수치는 Debug 에서 잰다. shader 는 모든 config 에서 같은 optimization 으로 compile 되므로 (check-gltf-brdf.py), Profile 에서도
같다고 본다 (추론).
"""

import argparse
import json
import math
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from directlightchecks import EDGE_PIXELS, LIGHT_OBJECT, accumulate_export, overlay, set_light
from gltfwriter import GLTF_FLOAT, GLTF_UNSIGNED_SHORT, BufferBuilder, flatten
from imagefiles import DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, ImageFileError, read_dds_rgba32f, read_dds_rgba8
from imagemasks import column_groups, eroded
from pathtracerchecks import BASE, FURNACE_SAMPLES, SECTION, SKY_LIGHT_OBJECT, SKY_RADIANCE, path_tracer_state, set_path_tracer
from viewer import (HARNESS_DIR, REPO_ROOT, SETTLE_FRAMES, SNAPSHOT_POSES, HarnessError, Session, baseline_scene_pose,
                    capture, start_instance, stop_instance, write_scene_file)

SCENE_DIR = HARNESS_DIR / 'probe-hit-shading'
ALBEDO_SCENE = 'ProbeHitAlbedo'
SHADOW_SCENE = 'ProbeHitShadow'

SHADING_FILE = 'MiniEngine/Core/Shaders/SurfaceShading.hlsli'
PATH_TRACER_SHADER = 'MiniEngine/Model/PathTracer/PathTracerCS.hlsl'
PROBE_HIT_FUNCTIONS = ['GetProbeHitAlbedo', 'GetProbeHitOutgoingRadiance', 'GetProbeHitIndirectWeight', 'GetDirectionalLightVisibility']
FORBIDDEN_PATH_TRACER_TEXT = ['0.04', '0.96', '0.95', '1.05', 'SampleCmp']
FORBIDDEN_SHADING_PATTERN = r'ddx|ddy|discard|clip\(|SV_'

PROBE_HIT = {'Shading': 'DDGI Probe Hit'}
LAMBERT = {'Shading': 'Lambert'}
GLTF = {'Shading': 'glTF'}
RADIANCE = {'Output': 'Radiance'}
IRRADIANCE = {'Output': 'Indirect Irradiance'}
OUTPUTS = ['Radiance', 'Indirect Irradiance']

# --- test scene ---
LIGHT_DIRECTION = (0.0, 0.8, 0.6)
INTENSITY = float(np.float32(math.pi))
TEST_SAMPLES = 16
TEST_DIRECT = BASE | RADIANCE | {SKY_RADIANCE: 0, 'Max Bounces': 1, 'Russian Roulette': 0, 'Target Samples': TEST_SAMPLES}
FRONT_FACE_COLOR = (0, 255, 0)
MIN_QUAD_PIXELS = 1000
QUAD_SPACING = 2.4
BASE_COLOR = (0.8, 0.5, 0.2)
ALBEDO_METALLIC = [0.0, 1.0]
ALBEDO_TOLERANCE = 1e-3
ALBEDO_FRACTION = 0.99
# [E1] 의 Lambert 가 metallic 1 에서 내는 0 처럼 기대값이 0 인 channel 은 상대 오차를 이 값에 대해 잰다.
ALBEDO_FLOOR = 1e-3
CHECKER_SIZE = 256
CHECKER_CELL = 32
# sRGB 8 bit. channel 마다 두 색이 크게 달라서 mip 0 을 읽으면 모든 channel 의 변동 계수가 크다.
CHECKER_COLORS = [(230, 51, 153), (51, 204, 26)]
CHECKER_FILE = 'checker.png'
MIP_FLAT = 0.01
MIP_TOLERANCE = 0.02
MIP_CONTRAST = 0.2
FLOOR_HALF_SIZE = 2.0
OCCLUDER_HALF_SIZE = 0.8
OCCLUDER_HEIGHT = 0.8
SHADOW_BASE_COLOR = (0.8, 0.8, 0.8)
LIT_PERCENTILE = 99
SHADOW_EDGE_PIXELS = 3
SHADOW_RASTER_AGREEMENT = 0.995
SHADOW_LAMBERT_AGREEMENT = 0.99
MIN_UNLIT_PIXELS = 1000

# --- Sponza ---
FURNACE_SKY = 1.0
FURNACE_ALBEDO = 0.9
# (a k)^(FURNACE_BOUNCES + 1) 이 FURNACE_TOLERANCE 보다 훨씬 작아서 Max Bounces 의 잘림은 무시할 수 있다.
FURNACE_BOUNCES = 256
FURNACE_TOLERANCE = 1e-4
FURNACE = BASE | PROBE_HIT | {'Furnace': 1, 'Furnace Albedo': FURNACE_ALBEDO, SKY_RADIANCE: FURNACE_SKY, 'Geometric Normals': 1,
                              'Russian Roulette': 0, 'Max Bounces': FURNACE_BOUNCES, 'Target Samples': FURNACE_SAMPLES}
FIRST_POINT_SAMPLES = 16
FIRST_POINT = BASE | IRRADIANCE | {'Max Bounces': 1, 'Seed': 0, 'Geometric Normals': 0, 'Russian Roulette': 1,
                                   'Target Samples': FIRST_POINT_SAMPLES}
# d05ec6f 를 두 번 띄워 쌓은 Lambert 의 export 는 rgb 가 4 ulp 까지 달랐다 (docs/reference-path-tracer-roadmap.md 의 뒤로 미룬 일). 그 두
# 배를 둔다.
LAMBERT_RUN_ULPS = 8
FIRST_POINT_POSES = ['start', 'courtyard-up']

RESET_SAMPLES = 32
RESET_LIGHT = 'elevation=35 azimuth=150'

# gallery 시점에서 light 를 낮춰 해가 닿는 곳과 가려진 곳이 같이 보이게 한다.
RESTORE_LINE = {
    'pose': SNAPSHOT_POSES['gallery'],
    'light': 'elevation=40 azimuth=200 color=1,0.9,0.8 intensity=3',
    'sky': 'Color=0.75,0.5,1; Intensity=0.5',
    'path tracer': PROBE_HIT | {'Output': 'Radiance', 'Seed': 5, 'Max Bounces': 8, 'Russian Roulette': 1, 'Geometric Normals': 0,
                                'Furnace': 0, 'Furnace Albedo': 0.75, 'Furnace Emission': 0.25},
    'samples': 16,
}
RESTORED_STATE = ['camera', 'light', 'sky', 'settings.Engine Tuning']


# --- SHADING_FILE 의 상수 ----------------------------------------------------------------------

def shading_constant(name):
    """SHADING_FILE 이 정한 float 상수. 곱으로 적은 값은 곱한다. 진실은 SHADING_FILE 에만 둔다."""
    text = (REPO_ROOT / SHADING_FILE).read_text(encoding='utf-8')
    match = re.search(rf'static const float {name} = ([0-9.* ]+);', text)
    if match is None:
        raise HarnessError(f'{SHADING_FILE} has no static const float {name}')
    return math.prod(float(factor) for factor in match[1].split('*'))


def probe_hit_albedo(base_color, metallic):
    base_color = np.asarray(base_color, dtype=np.float64)
    return (1.0 - metallic) * (0.96 * base_color + 0.04) + metallic * base_color


# --- test scene 의 asset -----------------------------------------------------------------------

def quad_positions(half_size, z):
    return [(-half_size, half_size, z), (half_size, half_size, z), (half_size, -half_size, z), (-half_size, -half_size, z)]


# glTF 의 앞면은 counter-clockwise 다. +z 에서 보면 FRONT_TO_PLUS_Z 의 두 triangle 이 counter-clockwise 다.
FRONT_TO_PLUS_Z = [0, 3, 2, 0, 2, 1]
FRONT_TO_MINUS_Z = [0, 2, 3, 0, 1, 2]
QUAD_UV = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]


def write_gltf(name, quads, materials, images=()):
    """quads 는 (positions, normal, indices, material, translation) 의 list 다."""
    builder = BufferBuilder()
    meshes, nodes = [], []
    for index, (positions, normal, indices, material, translation) in enumerate(quads):
        attributes = {'POSITION': builder.add(flatten(positions), GLTF_FLOAT, 'VEC3', 4, with_bounds=True),
                      'NORMAL': builder.add(flatten([normal] * 4), GLTF_FLOAT, 'VEC3', 4),
                      'TEXCOORD_0': builder.add(flatten(QUAD_UV), GLTF_FLOAT, 'VEC2', 4)}
        meshes.append({'name': f'quad {index}', 'primitives': [{
            'attributes': attributes, 'indices': builder.add(indices, GLTF_UNSIGNED_SHORT, 'SCALAR', 6), 'material': material}]})
        nodes.append({'name': f'quad {index}', 'mesh': index, 'translation': list(translation)})
    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/check-probe-hit-shading.py'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'buffers': [{'uri': f'{name}.bin', 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    if images:
        gltf['textures'] = [{'source': index} for index in range(len(images))]
        gltf['images'] = [{'uri': uri} for uri in images]
    (SCENE_DIR / f'{name}.bin').write_bytes(builder.data)
    (SCENE_DIR / f'{name}.gltf').write_text(json.dumps(gltf, indent=2), encoding='utf-8')


def write_checker():
    cells = (np.indices((CHECKER_SIZE, CHECKER_SIZE)) // CHECKER_CELL).sum(axis=0) % 2
    texture = np.array(CHECKER_COLORS, dtype=np.uint8)[cells]
    Image.fromarray(texture).save(SCENE_DIR / CHECKER_FILE)


def write_test_scenes():
    SCENE_DIR.mkdir(parents=True, exist_ok=True)
    write_checker()
    quads, materials = [], []
    for index, metallic in enumerate(ALBEDO_METALLIC):
        materials.append({'name': f'factor metallic {metallic:g}', 'pbrMetallicRoughness': {
            'baseColorFactor': [*BASE_COLOR, 1.0], 'metallicFactor': metallic, 'roughnessFactor': 0.5}})
        quads.append((quad_positions(1.0, 0.0), (0.0, 0.0, 1.0), FRONT_TO_PLUS_Z, index, ((index - 1) * QUAD_SPACING, 0.0, 0.0)))
    materials.append({'name': 'checker metallic 0', 'pbrMetallicRoughness': {
        'baseColorTexture': {'index': 0}, 'metallicFactor': 0.0, 'roughnessFactor': 0.5}})
    quads.append((quad_positions(1.0, 0.0), (0.0, 0.0, 1.0), FRONT_TO_PLUS_Z, len(materials) - 1, (QUAD_SPACING, 0.0, 0.0)))
    write_gltf(ALBEDO_SCENE, quads, materials, [CHECKER_FILE])

    materials = [{'name': 'floor', 'pbrMetallicRoughness': {'baseColorFactor': [*SHADOW_BASE_COLOR, 1.0], 'metallicFactor': 0.0,
                                                            'roughnessFactor': 0.5}},
                 {'name': 'occluder', 'pbrMetallicRoughness': {'baseColorFactor': [0.5, 0.5, 0.5, 1.0], 'metallicFactor': 0.0,
                                                               'roughnessFactor': 0.5}}]
    quads = [(quad_positions(FLOOR_HALF_SIZE, 0.0), (0.0, 0.0, 1.0), FRONT_TO_PLUS_Z, 0, (0.0, 0.0, 0.0)),
             (quad_positions(OCCLUDER_HALF_SIZE, 0.0), (0.0, 0.0, -1.0), FRONT_TO_MINUS_Z, 1, (0.0, 0.0, OCCLUDER_HEIGHT))]
    write_gltf(SHADOW_SCENE, quads, materials)


# --- app 조작 ------------------------------------------------------------------------------------

def export(session, settings, name):
    """settings 로 처음부터 쌓은 export 의 rgba 와 NaN 이나 Inf 라서 버린 sample 수"""
    rgba, invalid, _ = accumulate_export(session, settings, name)
    return rgba.astype(np.float64), invalid


def vector_of(fields):
    return np.array([float(fields[axis]) for axis in 'xyz'])


def set_test_light(session):
    direction = ','.join(f'{value:g}' for value in LIGHT_DIRECTION)
    session.ask_ok(f'light set direction={direction} color=1,1,1 intensity={INTENSITY!r} position=0,0,0')
    return vector_of(session.ask_ok('state light')['state']['light']['direction'])


def front_faces(session):
    return np.all(overlay(session, 'Debug: Front Face', 'front-face') == FRONT_FACE_COLOR, axis=2)


def describe_pixels(mask, limit=5):
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return 'none'
    listed = ', '.join(f'({x}, {y})' for x, y in list(zip(xs.tolist(), ys.tolist()))[:limit])
    return f'{len(xs)} at x {xs.min()}..{xs.max()}, y {ys.min()}..{ys.max()}: {listed}'


def relative_error(values, expected):
    expected = np.broadcast_to(np.asarray(expected, dtype=np.float64), values.shape)
    return np.abs(values - expected) / np.maximum(expected, ALBEDO_FLOOR)


def judge_albedo(values, expected):
    """(channel 마다 상대 오차의 중앙값, 모든 channel 이 ALBEDO_TOLERANCE 안인 pixel 의 비율, 벗어난 pixel 의 mask 안 순서)"""
    error = relative_error(values, expected)
    off = np.any(error > ALBEDO_TOLERANCE, axis=1)
    return np.median(error, axis=0), 1.0 - float(off.mean()), off


# --- [E1], [E2] --------------------------------------------------------------------------------

def mip_texel_albedo(lod):
    """app 이 checker 로 만든 DDS 의 mip lod 의 texel 하나를 linear 로 decode 한 ρ' (metallic 0, factor 1)"""
    dxgi_format, mips = read_dds_rgba8((SCENE_DIR / CHECKER_FILE).with_suffix('.dds'))
    mip = mips[lod]
    if dxgi_format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB or mip.shape[:2] != (1, 1):
        raise HarnessError(f'the checker DDS is DXGI format {dxgi_format} with mip {lod} of {mip.shape[1]}x{mip.shape[0]}, '
                           f'not sRGB with a 1x1 mip')
    srgb = mip[0, 0, :3].astype(np.float64) / 255.0
    linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    return probe_hit_albedo(linear, 0.0), mip[0, 0, :3]


def check_albedo_scene(session, check):
    light = set_test_light(session)
    front = eroded(front_faces(session), EDGE_PIXELS)
    masks = column_groups(front)
    sizes = [int(mask.sum()) for mask in masks]
    if not check(f'the albedo scene shows 3 quads of at least {MIN_QUAD_PIXELS} pixels after trimming {EDGE_PIXELS} edge pixels',
                 len(masks) == 3 and min(sizes) >= MIN_QUAD_PIXELS, f'{len(masks)} quads of {sizes} pixels'):
        return
    scale = INTENSITY * max(0.0, float(light[2])) / math.pi
    ratios, invalid = {}, 0
    for shading in (PROBE_HIT, LAMBERT):
        rgba, bad = export(session, TEST_DIRECT | shading | {'Geometric Normals': 1}, f'albedo-{shading["Shading"].lower().replace(" ", "-")}')
        ratios[shading['Shading']] = [rgba[mask][:, :3] / scale for mask in masks]
        invalid += bad

    for index, metallic in enumerate(ALBEDO_METALLIC):
        expected = probe_hit_albedo(BASE_COLOR, metallic)
        median, fraction, off = judge_albedo(ratios[PROBE_HIT['Shading']][index], expected)
        ys, xs = np.nonzero(masks[index])
        off_mask = np.zeros_like(masks[index])
        off_mask[ys[off], xs[off]] = True
        check(f'[E1] metallic {metallic:g}: the Probe Hit ratio is rho\' {np.round(expected, 6).tolist()} with a median relative error '
              f'<= {ALBEDO_TOLERANCE:g} per channel and {ALBEDO_FRACTION:.0%} of the pixels within it',
              bool(np.all(median <= ALBEDO_TOLERANCE)) and fraction >= ALBEDO_FRACTION and invalid == 0,
              f'median {np.array2string(median, precision=2)}, {fraction:.4%} of {len(off)} pixels within; '
              f'mean ratio {np.round(ratios[PROBE_HIT["Shading"]][index].mean(axis=0), 6).tolist()}; off pixels {describe_pixels(off_mask)}; '
              f'{invalid} invalid samples')

        lambert = ratios[LAMBERT['Shading']][index]
        lambert_expected = np.array(BASE_COLOR) * (1.0 - metallic)
        lambert_median, lambert_fraction, _ = judge_albedo(lambert, lambert_expected)
        against_probe = np.median(relative_error(lambert, expected), axis=0)
        check(f'[E1] metallic {metallic:g}: the Lambert ratio is base color (1 - metallic) by the same criterion and differs from rho\'',
              bool(np.all(lambert_median <= ALBEDO_TOLERANCE)) and lambert_fraction >= ALBEDO_FRACTION
              and bool(np.any(against_probe > ALBEDO_TOLERANCE)),
              f'median against base color (1 - metallic) {np.array2string(lambert_median, precision=2)}, {lambert_fraction:.4%} within; '
              f'median against rho\' {np.array2string(against_probe, precision=3)}')

    lod = int(shading_constant('kProbeHitTextureLod'))
    expected, texel = mip_texel_albedo(lod)
    probe, lambert = ratios[PROBE_HIT['Shading']][2], ratios[LAMBERT['Shading']][2]
    variation = probe.std(axis=0) / probe.mean(axis=0)
    deviation = np.abs(probe.mean(axis=0) / expected - 1.0)
    check(f'[E2] the Probe Hit ratio on the checker varies by {MIP_FLAT:.0%} or less per channel and its mean is the rho\' of the mip {lod} '
          f'texel within {MIP_TOLERANCE:.0%}',
          bool(np.all(variation <= MIP_FLAT)) and bool(np.all(deviation <= MIP_TOLERANCE)),
          f'coefficient of variation {np.array2string(variation, precision=4)}; mean {np.round(probe.mean(axis=0), 5).tolist()} vs '
          f'{np.round(expected, 5).tolist()} of texel {texel.tolist()}, relative {np.array2string(deviation, precision=4)}')
    contrast = lambert.std(axis=0) / lambert.mean(axis=0)
    check(f'[E2] the Lambert ratio on the checker shows both colors: a coefficient of variation of {MIP_CONTRAST:.0%} or more per channel',
          bool(np.all(contrast >= MIP_CONTRAST)), f'coefficient of variation {np.array2string(contrast, precision=3)}')


# --- [E3] --------------------------------------------------------------------------------------

def lit_and_unlit(rgb, floor):
    brightness = rgb.max(axis=2)
    lit = floor & (brightness > 0.5 * np.percentile(brightness[floor], LIT_PERCENTILE))
    return lit, floor & ~lit


def away_from_edges(lit, unlit):
    return eroded(lit, SHADOW_EDGE_PIXELS) | eroded(unlit, SHADOW_EDGE_PIXELS)


def shadow_agreement(first, second):
    """두 (lit, unlit) 의 경계에서 떨어진 pixel 에서 lit 이 같은 비율과 그 pixel 수"""
    counted = away_from_edges(*first) & away_from_edges(*second)
    same = counted & (first[0] == second[0])
    return float(same.sum()) / max(int(counted.sum()), 1), int(counted.sum())


def check_shadow_scene(session, check):
    set_test_light(session)
    set_path_tracer(session, {'Show': 0})
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    raster = read_dds_rgba32f(capture(session, 'scene', 'raster'))[..., :3]
    floor = eroded(front_faces(session), EDGE_PIXELS)
    probe, invalid_probe = export(session, TEST_DIRECT | PROBE_HIT, 'shadow-probe-hit')
    lambert, invalid_lambert = export(session, TEST_DIRECT | LAMBERT, 'shadow-lambert')
    set_path_tracer(session, {'Show': 0})

    masks = {name: lit_and_unlit(image[..., :3], floor) for name, image in [('raster', raster), ('probe', probe), ('lambert', lambert)]}
    with_raster, raster_pixels = shadow_agreement(masks['probe'], masks['raster'])
    with_lambert, lambert_pixels = shadow_agreement(masks['probe'], masks['lambert'])
    counts = {name: (int(lit.sum()), int(unlit.sum())) for name, (lit, unlit) in masks.items()}
    detail = f'lit and unlit pixels {counts}; {invalid_probe + invalid_lambert} invalid samples'
    check(f'[E3] Probe Hit and the raster agree on lit and unlit in {SHADOW_RASTER_AGREEMENT:.1%} or more of the floor pixels '
          f'{SHADOW_EDGE_PIXELS} pixels away from the edges', with_raster >= SHADOW_RASTER_AGREEMENT and counts['probe'][1] >= MIN_UNLIT_PIXELS
          and invalid_probe == 0, f'{with_raster:.4%} of {raster_pixels} pixels; {detail}')
    check(f'[E3] Probe Hit and Lambert (shadow ray) agree on lit and unlit in {SHADOW_LAMBERT_AGREEMENT:.0%} or more of those pixels',
          with_lambert >= SHADOW_LAMBERT_AGREEMENT and invalid_lambert == 0, f'{with_lambert:.4%} of {lambert_pixels} pixels; {detail}')


def run_test_scene(name, judge, build_first, check):
    instance = start_instance('Debug', scene=write_scene_file(SCENE_DIR / f'{name}.gltf'), build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            judge(session, check)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check(f'{name} ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)


# --- Sponza: [E4], [E5], [E10], [E11] -----------------------------------------------------------

def check_furnace(session, check, start_pose):
    session.ask_ok(f'pose {start_pose}')
    scale = shading_constant('kProbeHitIndirectScale')
    albedo = float(np.float32(FURNACE_ALBEDO))
    exact_emission = FURNACE_SKY * (1.0 - albedo * scale)
    for output in OUTPUTS:
        rgba, invalid = export(session, FURNACE | {'Output': output, 'Furnace Emission': exact_emission}, f'furnace-{output.lower().replace(" ", "-")}')
        hits = rgba[..., 3] > 0.0
        values = rgba[..., :3][hits] / (math.pi if output == IRRADIANCE['Output'] else 1.0)
        deviation = float(np.max(np.abs(values / FURNACE_SKY - 1.0)))
        check(f'[E4] {output}: the furnace with emission S (1 - a k) is S within {FURNACE_TOLERANCE:g} on every hit pixel',
              deviation <= FURNACE_TOLERANCE and invalid == 0 and hits.any(),
              f'{int(hits.sum())} hit pixels, max relative deviation {deviation:.3g}, k {scale:.9g}, emission {exact_emission:.9g}; '
              f'{invalid} invalid samples')

    lambert_emission = FURNACE_SKY * (1.0 - albedo)
    naive, invalid_naive = export(session, FURNACE | RADIANCE | {'Furnace Emission': lambert_emission}, 'furnace-lambert-emission')
    escaped, invalid_escaped = export(session, FURNACE | RADIANCE | {'Furnace Emission': 0}, 'furnace-no-emission')
    emission = float(np.float32(lambert_emission))
    limit = emission / (1.0 - albedo * scale)
    predicted = limit * (1.0 - escaped[..., :3] / FURNACE_SKY) + escaped[..., :3]
    deviation = float(np.max(np.abs(naive[..., :3] / predicted - 1.0)))
    shift = float(np.mean(naive[..., :3] / FURNACE_SKY - 1.0))
    check(f'[E4] Radiance: the furnace with emission S (1 - a) is L (1 - X_C / S) + X_C within {FURNACE_TOLERANCE:g}, and below S on average',
          deviation <= FURNACE_TOLERANCE and shift < -FURNACE_TOLERANCE and invalid_naive + invalid_escaped == 0,
          f'max relative deviation {deviation:.3g}; mean X_B / S - 1 {shift:.4%}, L / S - 1 {limit / FURNACE_SKY - 1.0:.4%}; '
          f'mean X_C / S {float(escaped[..., :3].mean()) / FURNACE_SKY:.4f}; {invalid_naive + invalid_escaped} invalid samples')


def ulps_between(first, second):
    # 양수 float 는 bit 를 정수로 읽은 차가 ulp 수다.
    return int(np.max(np.abs(first.view(np.int32).astype(np.int64) - second.view(np.int32).astype(np.int64))))


def compare_lambert_bits(now, reference):
    """(rgba 가 다른 pixel 수, rgb 의 최대 ulp 차, a 가 bit 까지 같은지)"""
    pixels = int(np.count_nonzero(np.any(now.view(np.uint32) != reference.view(np.uint32), axis=2)))
    return pixels, ulps_between(now[..., :3], reference[..., :3]), \
        bool(np.array_equal(now[..., 3].view(np.uint32), reference[..., 3].view(np.uint32)))


def check_first_point(session, check, default_light):
    details, passed = [], True
    for pose_name in FIRST_POINT_POSES:
        session.ask_ok(f'pose {SNAPSHOT_POSES[pose_name] or baseline_scene_pose()}')
        set_light(session, default_light)
        images = {}
        for shading in (PROBE_HIT, LAMBERT):
            rgba, _, _ = accumulate_export(session, FIRST_POINT | shading, f'first-point-{pose_name}-{shading["Shading"].lower().replace(" ", "-")}')
            images[shading['Shading']] = rgba
        pixels, ulps, counts_same = compare_lambert_bits(images[PROBE_HIT['Shading']], images[LAMBERT['Shading']])
        lit = int(np.count_nonzero(np.any(images[LAMBERT['Shading']][..., :3] != 0.0, axis=2)))
        passed = passed and counts_same and ulps <= LAMBERT_RUN_ULPS and lit > 0
        details.append(f'{pose_name}: {pixels} pixels differ by up to {ulps} ulps, a {"same" if counts_same else "differs"}, {lit} pixels not 0')
    check(f'[E5] Indirect Irradiance with Max Bounces 1: Probe Hit accumulates the bits of Lambert, rgb within {LAMBERT_RUN_ULPS} ulps',
          passed, '; '.join(details))


def check_resets(session, check, start_pose, default_light):
    session.ask_ok(f'pose {start_pose}')
    set_light(session, default_light)
    set_path_tracer(session, BASE | PROBE_HIT | {'Target Samples': 4096})
    session.ask_ok('pt reset')
    changes = [(f'light set {RESET_LIGHT}', 'the light direction'), (GLTF, 'Shading to glTF'), (PROBE_HIT, 'Shading back to DDGI Probe Hit')]
    for change, label in changes:
        session.ask_ok(f'pt wait samples={RESET_SAMPLES}', timeout=900)
        before = path_tracer_state(session)
        if isinstance(change, str):
            session.ask_ok(change)
        else:
            set_path_tracer(session, change)
        session.ask_ok('frames 2')
        after = path_tracer_state(session)
        check(f'[E11] changing {label} while accumulating DDGI Probe Hit starts over',
              after['resets'] == before['resets'] + 1 and after['samples'] < before['samples'],
              f'resets {before["resets"]} -> {after["resets"]}, samples {before["samples"]} -> {after["samples"]}')
    set_light(session, default_light)
    set_path_tracer(session, {'Show': 0})


def read_restored_state(session):
    state = {}
    for path in RESTORED_STATE:
        part = session.ask_ok(f'state {path}')['state']
        for key in path.split('.'):
            part = part[key]
        state[path] = part
    state['settings.Engine Tuning'] = {key: value for key, value in state['settings.Engine Tuning'].items() if key != 'Path Tracer/Show'}
    return state


def take_restore_line(session):
    """RESTORE_LINE 의 조건으로 쌓아 export 하고 (줄, export, state) 를 돌려준다."""
    line = RESTORE_LINE
    session.ask_ok(f'pose {line["pose"]}')
    session.ask_ok(f'light set {line["light"]}')
    session.ask_ok(f'editor set {SKY_LIGHT_OBJECT}: {line["sky"]}')
    set_path_tracer(session, line['path tracer'] | {'Show': 1, 'Samples per Frame': 1, 'Target Samples': line['samples']})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={line["samples"]}', timeout=900)
    reply = session.ask_ok(f'capture pt {Path(session.instance["run_dir"]) / "shots" / "restore-line"}')
    return reply['line'], read_dds_rgba32f(reply['path']), read_restored_state(session)


def parse_line(line):
    return dict(item.split('=', 1) for item in line.split('] ', 1)[-1].split('; '))


def check_restore(session, check, taken):
    line, image, state = taken
    session.ask_ok(f'restore {line}')
    now = parse_line(session.ask_ok('describe')['line'])
    differing = {key: f'{value} vs {now.get(key)}' for key, value in parse_line(line).items() if key != 'pt samples' and now.get(key) != value}
    restored = read_restored_state(session)
    differing_state = [path for path in RESTORED_STATE if restored[path] != state[path]]
    check('[E10] restoring the DDGI Probe Hit line after a restart gives its conditions and state again', not differing and not differing_state,
          f'line {differing or "same"}; state {", ".join(differing_state) or "same"}')
    set_path_tracer(session, {'Show': 1})
    session.ask_ok(f'pt wait samples={RESTORE_LINE["samples"]}', timeout=900)
    again = read_dds_rgba32f(capture(session, 'pt', 'restore-line-restarted'))
    differing_pixels = int(np.count_nonzero(np.any(again.view(np.uint32) != image.view(np.uint32), axis=2))) if again.shape == image.shape else None
    check(f'[E10] the DDGI Probe Hit export after the restart is bit-identical to the one before, at {RESTORE_LINE["samples"]} samples',
          differing_pixels == 0, f'{differing_pixels} pixels differ' if differing_pixels is not None else f'shape {again.shape} vs {image.shape}')


def run_sponza(check, build_first):
    instance = start_instance('Debug', build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            default_light = session.ask_ok(f'editor get {LIGHT_OBJECT}')['values']
            start_pose = baseline_scene_pose()
            session.ask_ok(f'pose {start_pose}')
            set_light(session, default_light)
            check_furnace(session, check, start_pose)
            check_first_point(session, check, default_light)
            check_resets(session, check, start_pose, default_light)
            taken = take_restore_line(session)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('Sponza ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
        return

    instance = start_instance('Debug', build_first=False)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_restore(session, check, taken)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('Sponza after the restart ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)


# --- source: [E6] ------------------------------------------------------------------------------

def git_grep(*arguments):
    return subprocess.run(['git', '-c', 'core.quotepath=false', 'grep', '--untracked', '-n', *arguments], cwd=REPO_ROOT,
                          capture_output=True, text=True, encoding='utf-8').stdout.splitlines()


def probe_hit_sections(text):
    """PROBE_HIT_SHADING 이 참일 때만 compile 되는 줄. #if PROBE_HIT_SHADING 부터 그 짝의 #else 나 #endif 까지다."""
    lines, depth, inside = [], 0, False
    for line in text.splitlines():
        directive = line.strip()
        if inside:
            if directive.startswith('#if'):
                depth += 1
            elif directive.startswith('#endif') and depth > 0:
                depth -= 1
            elif depth == 0 and (directive.startswith('#else') or directive.startswith('#elif') or directive.startswith('#endif')):
                inside = False
                continue
            lines.append(line)
        elif re.match(r'#if\s+PROBE_HIT_SHADING\b', directive):
            inside, depth = True, 0
    return lines


def check_source(check):
    names = '|'.join(PROBE_HIT_FUNCTIONS)
    definitions = [line for line in git_grep('-E', rf'^\s*[A-Za-z_][A-Za-z0-9_]*\s+({names})\s*\(', '--', '*.hlsl', '*.hlsli')
                   if not re.search(r':\s*return\s', line)]
    defined_in = {name: sorted({line.split(':', 1)[0] for line in definitions if re.search(rf'\b{name}\s*\(', line)}) for name in PROBE_HIT_FUNCTIONS}
    path_tracer = (REPO_ROOT / PATH_TRACER_SHADER).read_text(encoding='utf-8')
    called = [name for name in PROBE_HIT_FUNCTIONS if re.search(rf'\b{name}\s*\(', path_tracer)]
    sections = probe_hit_sections(path_tracer)
    formulas = [line.strip() for line in sections if not line.strip().startswith('//') and re.search(r'\bkPi\b|\bdot\(', line)]
    left = [text for text in FORBIDDEN_PATH_TRACER_TEXT if text in path_tracer]
    check('[E6] the probe hit shading is defined only in SurfaceShading.hlsli, the path tracer calls it and has no formula or constant of its own',
          all(files == [SHADING_FILE] for files in defined_in.values()) and called == PROBE_HIT_FUNCTIONS and sections and not formulas and not left,
          f'defined in {defined_in}; PathTracerCS calls {called}; {len(sections)} lines under PROBE_HIT_SHADING, formulas there {formulas}; '
          f'{FORBIDDEN_PATH_TRACER_TEXT} found {left}')
    shading = (REPO_ROOT / SHADING_FILE).read_text(encoding='utf-8')
    found = [f'{number}: {line.strip()}' for number, line in enumerate(shading.splitlines(), 1) if re.search(FORBIDDEN_SHADING_PATTERN, line)]
    check(f'[E6] {SHADING_FILE} has no {FORBIDDEN_SHADING_PATTERN}', not found, '; '.join(found) or 'none')


# --- 명령 --------------------------------------------------------------------------------------

def command_check(args):
    check = Checks()
    write_test_scenes()
    run_test_scene(ALBEDO_SCENE, check_albedo_scene, not args.no_build, check)
    run_test_scene(SHADOW_SCENE, check_shadow_scene, False, check)
    run_sponza(check, False)
    check_source(check)
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    for name, handler, help in [('check', command_check, 'judge the criteria and the D3D12 messages in Debug')]:
        command = commands.add_parser(name, help=help)
        command.add_argument('--no-build', action='store_true', help='use the exe as it is')
        command.set_defaults(handler=handler)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (HarnessError, ImageFileError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
