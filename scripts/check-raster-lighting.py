"""raster 가 그리는 빛이 directional light 와 emission 뿐이고, raster 가 sky 를 그리지 않는지 확인한다.

  python scripts/check-raster-lighting.py check   # [1] ~ [5], [7], [8], [10], [13], [14] 를 판정한다

번호는 작업 C 의 완료 기준의 번호다. [6] 과 [9] 는 Environment 를 빼면서 없앴고, [11] 의 build 와 [12] 의 viewer.py selftest 는 이
script 밖에서 본다.

app 을 Debug 로 세 번 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1 이다. 실행마다 끝낼 때 실행
중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다.

판정은 harness baseline 에서 UI 를 숨기고 찍은 scene capture 로 한다. scene capture 는 post effects 전의 scene color 이고, 같다는 것은
float 의 bit 가 같다는 뜻이다.

test scene 은 xy 평면의 quad 셋을 x 축을 따라 놓은 것이다. -model 의 OrbitCamera 는 model 중심의 +z 쪽에서 -z 를 본다
(OrbitCamera::Update). 모든 material 은 흰 baseColor, metallic 0, roughness 1 이고, light 는 light set direction=LIGHT_DIRECTION 으로
quad 의 뒤쪽 위에 둔다.

  dark: +z 를 향해 light 를 등지고, emission 이 없다.
  emissive: dark 와 같고 emissiveFactor 가 EMISSIVE 다. 세 값은 R11G11B10 이 정확히 담는 값이다.
  lit: x 축으로 -45° 돌려 normal 이 (0, sin 45°, cos 45°) 다. light 를 향한다.

quad 의 pixel 은 Ray Tracing View 의 Front Face capture 에서 앞면인 pixel 의 가장자리 EDGE_PIXELS 를 깎고 column 이 이어진 덩어리로
나눠 왼쪽부터 dark, emissive, lit 로 본다. 덩어리가 셋이 아니거나 한 덩어리가 MIN_PIXELS 보다 작으면 [1] ~ [3] 을 판정하지 않고
실패한다.

  [1] dark 의 모든 pixel 이 (0, 0, 0) 이다. raster 에 directional light 와 emission 밖의 빛이 없다.
  [2] emissive 의 모든 pixel 이 EMISSIVE 다. emission 은 남는다.
  [3] lit 의 pixel 평균이 세 channel 모두 0 보다 크다. [1] 이 아무것도 그리지 않은 화면으로 통과하지 않게 한다.
  [4] 모든 material 에 occlusion texture (모든 texel 0) 를 붙인 test scene 의 capture 가 [1] ~ [3] 의 capture 와 같다.

Sponza 에는 emissive 와 occlusion texture 가 없어서 그 둘은 test scene 으로 본다.

  [5] SKY_POSE 에서 Ray Tracing View 의 Front Face capture 가 아무 면도 만나지 않은 pixel 의 가장자리 EDGE_PIXELS 를 깎은 것 (sky
      pixel) 이 scene capture 에서 모두 (0, 0, 0) 이다. raster 는 작업 9 전까지 sky 를 그리지 않는다. sky pixel 이 화면의
      MIN_SKY_FRACTION 보다 적으면 실패한다.
  [7] profile Generate SSAO 가 실패한다. SSAO pass 가 돌지 않는다.
  [8] Engine Tuning 에 Viewer/Lighting/Environment 와 Viewer/Lighting/Gloss Reduction 이 없다.
  [13] harness baseline 의 시점에서 camera 를 멈춘 채 motion blur 를 켜고 찍은 capture 가 끄고 찍은 capture 와 같다. motion blur 는
       g_LinearDepth 로 camera 의 velocity 를 구하므로 (ModelViewer::RenderScene), SSAO 를 빼도 linear depth 가 이번 frame 의 depth
       로 채워지는지 본다. 끄고 찍은 capture 에서 0 이 아닌 pixel 이 MIN_LIT_FRACTION 보다 적으면 실패한다.

source:

  [10] ModelViewer 의 source 와 Model/Renderer 에 FORBIDDEN_NAMES 가 없다. raster 가 irradiance cube, SSAO, Gloss Reduction,
       Environment 의 cubemap, skybox 를 쓰지 않는다.
  [14] FETCH_SCRIPT 가 CUBEMAP_FOLDER 를 받지 않는다.

Sponza 의 SKY_POSE capture 는 발표에 쓰도록 KEPT_DIR 에 DDS 와 보기용 PNG 로 남긴다.
"""

import argparse
import json
import math
import shutil
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
from viewer import (ENGINE_DIR, HARNESS_DIR, PREVIEW_EXPOSURE, REPO_ROOT, SETTLE_FRAMES, VIEWER_DIR, HarnessError, Session, capture,
                    describe_difference, make_view, start_instance, stop_instance)

SCENE_DIR = HARNESS_DIR / 'raster-lighting'
KEPT_DIR = HARNESS_DIR / 'kept' / 'raster-lighting'
TEST_SCENES = {'plain': 'RasterLighting', 'occluded': 'RasterLightingOccluded'}

# light 와 normal 의 cos 는 +z 를 향한 dark 와 emissive 에서 -0.2 / |(0, 1, -0.2)| 이고, lit 에서 (sin 45° - 0.2 cos 45°) / |(0, 1, -0.2)| 다.
LIGHT_DIRECTION = (0.0, 1.0, -0.2)
EMISSIVE = (0.5, 0.25, 0.125)
OCCLUSION_TEXTURE_SIZE = 4
FRONT_FACE_COLOR = (0, 255, 0)
EDGE_PIXELS = 2
MIN_PIXELS = 500

QUAD_POSITIONS = [(-1.0, 1.0, 0.0), (1.0, 1.0, 0.0), (1.0, -1.0, 0.0), (-1.0, -1.0, 0.0)]
QUAD_NORMAL = (0.0, 0.0, 1.0)
QUAD_UV = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
# glTF 의 앞면은 counter-clockwise 다. +z 에서 보면 두 triangle 모두 counter-clockwise 다.
QUAD_INDICES = [0, 3, 2, 0, 2, 1]

QUADS = [
    SimpleNamespace(name='dark', emissive=(0.0, 0.0, 0.0), translation=(-3.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)),
    SimpleNamespace(name='emissive', emissive=EMISSIVE, translation=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)),
    SimpleNamespace(name='lit', emissive=(0.0, 0.0, 0.0), translation=(3.0, 0.0, 0.0),
                    rotation=(-math.sin(math.pi / 8), 0.0, 0.0, math.cos(math.pi / 8))),
]

# 기본 시점의 위치를 courtyard 가운데로 옮기고 위를 본다. 가장자리를 깎은 sky pixel 이 화면의 약 24% 다.
SKY_POSE = '0 300 0 1.5708 1.2'
MIN_SKY_FRACTION = 0.1
# harness baseline 의 시점에서 해가 닿는 pixel 은 약 7% 다.
MIN_LIT_FRACTION = 0.01
MOTION_BLUR_SETTING = 'Graphics/Motion Blur/Enable'
SSAO_SCOPE = 'Generate SSAO'
PROFILE_FRAMES = 4
REMOVED_SETTINGS = ['Viewer/Lighting/Environment', 'Viewer/Lighting/Gloss Reduction']

FORBIDDEN_NAMES = ['_diffuseIBL', '_specularIBL', 'irradianceIBL', 'texSSAO', 'SSAO::', 'IBLBias', 'Gloss Reduction', 'Skybox']
SOURCE_SUFFIXES = {'.cpp', '.h', '.hlsl', '.hlsli'}
FETCH_SCRIPT = REPO_ROOT / 'scripts' / 'fetch-assets.ps1'
CUBEMAP_FOLDER = 'MiniEngine/ModelViewer/Textures'


def write_test_scene(name, occluded):
    builder = BufferBuilder()
    vertex_count = len(QUAD_POSITIONS)
    position = builder.add(flatten(QUAD_POSITIONS), GLTF_FLOAT, 'VEC3', vertex_count, with_bounds=True)
    normal = builder.add(flatten([QUAD_NORMAL] * vertex_count), GLTF_FLOAT, 'VEC3', vertex_count)
    uv = builder.add(flatten(QUAD_UV), GLTF_FLOAT, 'VEC2', vertex_count)
    indices = builder.add(QUAD_INDICES, GLTF_UNSIGNED_SHORT, 'SCALAR', len(QUAD_INDICES))

    materials, meshes, nodes = [], [], []
    for quad in QUADS:
        material = {
            'name': quad.name,
            'pbrMetallicRoughness': {'baseColorFactor': [1.0, 1.0, 1.0, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 1.0},
            'emissiveFactor': list(quad.emissive),
        }
        if occluded:
            material['occlusionTexture'] = {'index': 0}
        materials.append(material)
        meshes.append({'name': quad.name, 'primitives': [{
            'attributes': {'POSITION': position, 'NORMAL': normal, 'TEXCOORD_0': uv}, 'indices': indices, 'material': len(materials) - 1}]})
        nodes.append({'name': quad.name, 'mesh': len(meshes) - 1, 'translation': list(quad.translation), 'rotation': list(quad.rotation)})

    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/check-raster-lighting.py'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'buffers': [{'uri': f'{name}.bin', 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    if occluded:
        gltf['textures'] = [{'source': 0}]
        gltf['images'] = [{'uri': 'occlusion.png'}]
        texture = np.zeros((OCCLUSION_TEXTURE_SIZE, OCCLUSION_TEXTURE_SIZE, 3), dtype=np.uint8)
        Image.fromarray(texture).save(SCENE_DIR / 'occlusion.png')
    (SCENE_DIR / f'{name}.bin').write_bytes(builder.data)
    (SCENE_DIR / f'{name}.gltf').write_text(json.dumps(gltf, indent=2), encoding='utf-8')


def run_test_scene(scene, build_first, check):
    """test scene 의 scene capture rgb 와 Front Face capture. app 이 끝까지 돌지 않으면 None 이다."""
    instance = start_instance('Debug', model=f'{SCENE_DIR.relative_to(VIEWER_DIR).as_posix()}/{TEST_SCENES[scene]}.gltf',
                              build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok('light set direction=' + ','.join(f'{value:g}' for value in LIGHT_DIRECTION))
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            rgb = read_dds_rgba32f(capture(session, 'scene', f'{scene}-scene'))[..., :3]
            session.ask_ok('settings set Ray Tracing View: View=Debug: Front Face')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            front_face = np.asarray(Image.open(capture(session, 'overlay', f'{scene}-front-face')).convert('RGB'))
            session.ask_ok('settings set Ray Tracing View: View=Off')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check(f'the {scene} test scene ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
        return None
    return SimpleNamespace(rgb=rgb, front_face=front_face)


def count_not(pixels, value):
    return int(np.count_nonzero(np.any(pixels != value, axis=1)))


def judge_test_scenes(runs, check):
    plain, occluded = runs['plain'], runs['occluded']
    if plain is not None:
        front = np.all(plain.front_face == FRONT_FACE_COLOR, axis=2)
        groups = column_groups(eroded(front, EDGE_PIXELS))
        sizes = [int(group.sum()) for group in groups]
        found = check(f'the test scene shows {len(QUADS)} quads of at least {MIN_PIXELS} pixels', len(groups) == len(QUADS) and
                      min(sizes, default=0) >= MIN_PIXELS, f'{len(groups)} groups of {sizes} pixels after trimming {EDGE_PIXELS} edge pixels')
        if found:
            dark, emissive, lit = (plain.rgb[group] for group in groups)
            check('[1] the quad facing away from the light without emission is (0, 0, 0)', count_not(dark, 0.0) == 0,
                  f'{count_not(dark, 0.0)} of {len(dark)} pixels are not; max {dark.max():.9g}')
            check(f'[2] the emissive quad facing away from the light is {EMISSIVE}', count_not(emissive, EMISSIVE) == 0,
                  f'{count_not(emissive, EMISSIVE)} of {len(emissive)} pixels are not; mean {np.round(emissive.mean(axis=0), 9).tolist()}')
            mean = lit.mean(axis=0)
            check('[3] the quad facing the light has a mean above 0 in every channel', bool(np.all(mean > 0.0)),
                  f'mean {np.round(mean, 9).tolist()}')
    if plain is not None and occluded is not None:
        same = np.array_equal(plain.rgb.view(np.uint32), occluded.rgb.view(np.uint32))
        check('[4] an occlusion texture of 0 on every material changes no pixel', same, describe_difference(plain.rgb, occluded.rgb))


def keep(path):
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    kept = KEPT_DIR / path.name
    shutil.copy2(path, kept)
    make_view(kept, 'scene', kept.with_suffix('.png'), None, 1 << 16, PREVIEW_EXPOSURE)


def check_sky(session, check):
    scene_path = capture(session, 'scene', 'sponza-sky-pose')
    rgb = read_dds_rgba32f(scene_path)[..., :3]
    keep(scene_path)
    session.ask_ok('settings set Ray Tracing View: View=Debug: Front Face')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    front_face = np.asarray(Image.open(capture(session, 'overlay', 'sponza-sky-pose-front-face')).convert('RGB'))
    session.ask_ok('settings set Ray Tracing View: View=Off')

    sky = eroded(np.all(front_face == 0, axis=2), EDGE_PIXELS)
    drawn = sky & np.any(rgb != 0.0, axis=2)
    fraction = float(sky.mean())
    check('[5] the raster draws no sky', not drawn.any() and fraction >= MIN_SKY_FRACTION,
          f'{int(sky.sum())} sky pixels ({fraction:.1%} of the scene, at least {MIN_SKY_FRACTION:.0%} needed), '
          f'{int(drawn.sum())} of them are not (0, 0, 0)')


def check_settings(session, check):
    measured = session.ask(f'profile {SSAO_SCOPE} frames={PROFILE_FRAMES}')
    check(f'[7] profile {SSAO_SCOPE} fails because SSAO does not run', not measured['ok'], measured.get('error', measured))

    tuning = session.ask_ok('state settings.Engine Tuning')['state']['settings']['Engine Tuning']
    left = [name for name in REMOVED_SETTINGS if name in tuning]
    check('[8] Engine Tuning has none of ' + ', '.join(REMOVED_SETTINGS), not left, ', '.join(left))


def motion_blur_capture(session, enabled, name):
    session.ask_ok(f'settings set Engine Tuning: {MOTION_BLUR_SETTING}={"on" if enabled else "off"}')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    return read_dds_rgba32f(capture(session, 'scene', name))[..., :3]


def check_motion_blur(session, check):
    still = motion_blur_capture(session, False, 'sponza-motion-blur-off')
    blurred = motion_blur_capture(session, True, 'sponza-motion-blur-on')
    session.ask_ok(f'settings set Engine Tuning: {MOTION_BLUR_SETTING}=off')
    lit = float(np.any(still != 0.0, axis=2).mean())
    same = np.array_equal(still.view(np.uint32), blurred.view(np.uint32))
    check('[13] motion blur with the camera still changes no pixel', same and lit >= MIN_LIT_FRACTION,
          f'{describe_difference(still, blurred)}; {lit:.1%} of the pixels are lit, at least {MIN_LIT_FRACTION:.0%} needed')


def run_sponza(build_first, check):
    instance = start_instance('Debug', build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_motion_blur(session, check)
            session.ask_ok(f'pose {SKY_POSE}')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            check_sky(session, check)
            check_settings(session, check)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('Sponza ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)


def source_files():
    candidates = [*VIEWER_DIR.glob('*'), *(ENGINE_DIR / 'Model' / 'Renderer').glob('*')]
    return sorted(path for path in candidates if path.suffix in SOURCE_SUFFIXES)


def check_sources(check):
    found = []
    for path in source_files():
        for number, line in enumerate(path.read_text(encoding='utf-8-sig', errors='replace').splitlines(), start=1):
            found += [f'{path.relative_to(ENGINE_DIR).as_posix()}:{number} {name}' for name in FORBIDDEN_NAMES if name in line]
    check('[10] the sources have none of ' + ', '.join(FORBIDDEN_NAMES), not found, '; '.join(found) if found else '')
    check(f'[14] {FETCH_SCRIPT.name} does not fetch {CUBEMAP_FOLDER}', CUBEMAP_FOLDER not in FETCH_SCRIPT.read_text(encoding='utf-8-sig'))


def command_check(args):
    SCENE_DIR.mkdir(parents=True, exist_ok=True)
    for scene, name in TEST_SCENES.items():
        write_test_scene(name, occluded=scene == 'occluded')

    check = Checks()
    build_first = not args.no_build
    runs = {}
    for scene in TEST_SCENES:
        runs[scene] = run_test_scene(scene, build_first, check)
        build_first = False
    judge_test_scenes(runs, check)
    run_sponza(build_first, check)
    check_sources(check)
    print(f'kept captures: {KEPT_DIR}')
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    check_parser = commands.add_parser('check', help='judge [1] ~ [5], [7], [8], [10], [13], [14] with the Debug app')
    check_parser.add_argument('--no-build', action='store_true', help='run the Debug app as it is built')
    check_parser.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
