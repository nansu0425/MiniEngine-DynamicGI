"""외부 renderer (Mitsuba 3) 가 원본 Mitsuba scene 을 그린 image 와 path tracer (PT) 를 비교한다.

  python -I scripts/check-external-renderer.py accumulate      # PT 와 Mitsuba 로 쌓아 KEPT_DIR 에 남긴다
  python -I scripts/check-external-renderer.py check [--record]  # 남긴 것으로 판정한다

번호는 작업 13 (b) 의 완료 기준 번호다. 받은 Mitsuba scene 을 읽으므로 python -I 로 돌린다. accumulate 는 app 을 직접 띄우고 끝내며,
이미 띄운 harness instance 는 먼저 끝낸다. check 는 app 과 Mitsuba 를 띄우지 않는다. 하나라도 실패하면 exit 1 이다.

검증하는 범위: PT 의 정답 (Shading glTF) 이 맞으려면 glTF BRDF 자체, path loop 안의 glTF BRDF, 그리고 나머지 전부가 맞아야 한다. 나머지는
camera ray 와 pixel 격자, asset 변환과 load (scripts/cornellbox.py 의 변환 포함), 교차와 visibility, ray offset, emission, 색이 있는
throughput 과 Russian roulette, 누적이다. 이 나머지는 Shading glTF 와 Lambert 가 함께 쓰는 code 다. PathTracerCS.hlsl 에서 Shading 마다
다른 곳은 vertex 의 재질 값, directional light 의 BRDF 평가, 다음 방향 고르기뿐이다. furnace 는 scene 의 어디서나 radiance 가 같아 ray 가
어디로 가든 답이 같으므로 이 나머지의 오류를 잡지 못한다. 이 비교는 Shading Lambert 로 이 나머지를 검증한다. Lambert 는 Mitsuba 원본의
diffuse 와 같은 식이다. Mitsuba 는 변환 전의 원본을 그리므로 변환의 오류도 드러난다.

검증하지 않는 범위와 그것을 다루는 check:
- PathTracerCS.hlsl 에서 Shading glTF 일 때만 도는 code: GetPathVertex 가 MakeGltfBrdfInput 을 부르는 곳, ReflectDirectionalLight 의
  EvaluateGltfBrdf, TraceRadiance 가 SampleGltfBrdfDirection 이 고른 방향으로 다음 ray 를 쏘고 그 weight 를 throughput 에 곱하는 곳이다.
  이 code 가 부르는 GltfBrdf.hlsli 의 함수는 check-gltf-brdf.py 의 [B1] ~ [B5] 가 본다. 앞의 두 곳은 같은 script 의 [S3] 가 처음 맞은
  점의 directional light 직접광으로 본다. 다음 ray 를 쏘는 곳은 glTF furnace [S1], [S2] 만 본다. furnace 는 빛이 어느 방향에서나 같게
  들어오므로, 다음 ray 가 SampleGltfBrdfDirection 이 고른 방향과 다른 방향으로 나가도 답이 같아 잡지 못한다.
- directional light 와 Sky: 원본에 없어 intensity 0 으로 두므로 보지 않는다. check-directional-light.py 와 check-sky.py 가 본다.
- Max Bounces 64 를 넘는 path 의 몫: check-convergence.py 의 [C3] 이 본다.

조건: 측정용 Cornell box scene file (SCENE) 의 camera, 세로 FOV, light, sky (둘 다 intensity 0). PT 는 Profile, window client 를 원본
film 의 크기 (1024x1024) 로 둔 뒤 ui hide, COMPARISON 의 설정 (Shading Lambert, Output Radiance, Russian roulette on, Geometric Normals
off) 과 Seed 0, 1 이다. Mitsuba 는 MITSUBA_VARIANT 로 원본 scene 을 그리되 아래 넷만 바꾼다. 바꾼 scene 은 KEPT_DIR 에 남긴다.

- film 의 rfilter: tent 를 box 로. box 는 sample 을 그 sample 이 든 pixel 에만 같은 무게로 더하므로, pixel 의 값이 pixel 안의 radiance
  평균의 추정이 된다. PT 의 pixel 과 같다.
- 반 pixel 옮기기: PT 의 pixel 은 raster 의 pixel 이고, TAA 를 끄면 viewport 에서 반 pixel 왼쪽 위로 옮겨져 있다 (CameraRays.hlsli 의
  GetViewportRay). frustum 을 pixel 단위로 [0, W] 로 재면 PT 의 pixel x 는 [x - 1/2, x + 1/2] 를 덮는다. Mitsuba 는 film 의 한 변을 W' = 2W + 2 로, 시야각을 tan(fov' / 2) = tan(fov / 2) W' / (2W)
  로 그린다. 그러면 Mitsuba 의 pixel j 는 [(j - 1) / 2, j / 2] 를 덮으므로, PT 의 pixel x 는 Mitsuba 의 pixel 2x 와 2x + 1 의 평균이다.
  세로도 같다. 원본 film 이 정사각형이고 fov 가 x 축의 값 (fov_axis 가 없다) 일 때만 이 식이 맞으므로 [X4] 가 그것도 본다.
  Mitsuba 의 sub-pixel 규약에 의존하지 않고 film 의 크기와 시야각만 바꾸는 방법이다.
- sampler 의 spp 와 seed: Mitsuba 의 pixel 마다 SAMPLES / 4 sample 을 MITSUBA_PASS_SAMPLES 씩 나눠 그린다. pass 마다 seed 가 다르고
  pixel 의 값은 pass 들의 평균이다. 그래서 PT 의 pixel 하나에 PT 와 같은 SAMPLES 개의 sample 이 든다.
- integrator 의 max_depth: PT 의 Max Bounces n 은 Mitsuba 의 max_depth n + 1 이다 (PathTracer.h 의 maxBounces).

  [X1] Max Bounces 0: imagecompare.bias(PT Seed 0, PT Seed 1, Mitsuba Seed 0, Mitsuba Seed 1) 의 d 상한, b 상한, 두 floor 가 모두
       BIAS_BUDGET 이하다.
  [X2] Max Bounces 1: 같은 판정.
  [X3] Max Bounces 64: 같은 판정.
  [X4] run 이 유효하다. PT export 의 a 가 모든 pixel 에서 dispatch 한 sample 수 (SAMPLES) 다. Mitsuba image 가 유한하고 0 이상이다.
       Mitsuba 가 그린 scene 이 위 네 가지 밖에서는 원본 XML 과 같고, 바꾼 film 의 크기와 fov 가 위 식의 값이다. PT run 의 state 에서
       scene 크기가 원본 film 의 크기이고, camera 와 세로 FOV 가 scene file 과 float32 로 같고, Directional Light 와 Sky 의 intensity 가
       0 이며, PT 설정이 조건과 같다. app 이 정상 종료했다.
  [X5] accumulate 가 깊이마다 PT, Mitsuba, 둘의 차의 PNG 를 같은 exposure (PREVIEW_EXPOSURE) 로 남긴다 (발표용). check --record 는 [X4] 가
       통과하면 [X1] ~ [X3] 이 실패해도 측정 기록에 깊이마다 d, b, 상한, floor, spp, 각 renderer 의 한 run 의 noise
       (imagecompare.relative_noise), Mitsuba 의 version 과 variant 를 덧붙인다.
  [X6] 깊이마다, Mitsuba scene 의 shape 마다, shape 안쪽 pixel 만으로 낸 imagecompare.bias 의 d 상한과 floor 가 BIAS_BUDGET 이하다.
       안쪽 pixel 은 가운데, 네 변의 가운데, 네 모서리의 sample 이 모두 그 shape 의 같은 면을 맞는 pixel 이다. 면이 볼록하므로 pixel
       전체가 그 면을 본다. b 는 image 전체의 rms 로 나누어, 화면의 0.5% 인 light pixel 이 그 rms 를 지배한다. 그래서 어두운 면의 차에
       둔감하므로 shape 마다 그 shape 의 평균으로 나눈 d 를 본다. 두 renderer 가 그 pixel 에서 모두 0 이면 같다. PT 의 합이 0 인데
       Mitsuba 가 0 이 아니면 실패다. floor 가 BIAS_BUDGET 을 넘는 (깊이, shape) 는 그 spp 로 판정할 수 없으므로 판정하지 않고 floor 를
       알린다. Max Bounces 0, 1 의 천장처럼 빛이 거의 닿지 않는 shape 가 그렇다. 대신 모든 shape 가 한 깊이 이상에서 판정되어야 한다.
"""

import argparse
import json
import math
import shutil
import sys
import time
import xml.etree.ElementTree as ElementTree
from datetime import date
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cornellbox
from checks import Checks
from imagecompare import bias, relative_noise
from imagefiles import ImageFileError, read_dds_rgba32f
from pickanswers import PIXEL_CORNER_OFFSETS, PIXEL_EDGE_OFFSETS, seen_faces
from pathtracerchecks import (BASE, BIAS_BUDGET, SKY_COLOR, SKY_RADIANCE, append_record_row, code_version, describe_machine,
                              path_tracer_settings, set_path_tracer)
from viewer import (CAMERA_OBJECT, HARNESS_DIR, PREVIEW_EXPOSURE, VIEWER_DIR, HarnessError, Session, baseline_scene, read_scene_file, shots_dir,
                    start_instance, stop_instance, tone_map_for_view)

SCENE = baseline_scene('CornellBox.ini')
KEPT_DIR = HARNESS_DIR / 'kept' / 'external-renderer'
MANIFEST = KEPT_DIR / 'manifest.json'
RECORD_SECTION = '## 작업 13: 외부 renderer 와의 비교'

BOUNCES = {'X1': 0, 'X2': 1, 'X3': 64}
SEEDS = (0, 1)
# 256 spp 의 짧은 run 에서 가장 큰 floor 는 Max Bounces 64 의 b floor 4.6e-3 이었다. floor 는 sample 수의 제곱근에 반비례하므로
# (imagecompare.py), 예측한 floor 가 BIAS_BUDGET 의 1/3 아래로 내려가는 가장 작은 2 의 거듭제곱을 골랐다. 16384 spp 에서 5.8e-4 다.
SAMPLES = 1 << 14
# Sky 는 scene file 의 값 (0) 을 쓰므로 BASE 에서 뺀다. Samples per Frame 은 image 를 바꾸지 않는다 (check-path-tracer-export.py 의 [S1]).
COMPARISON = {key: value for key, value in BASE.items() if key not in (SKY_COLOR, SKY_RADIANCE)} | {'Samples per Frame': 16}
MITSUBA_VARIANT = 'cuda_ad_rgb'
# 2050x2050 의 film 에 pass 하나로 그리는 spp. 이보다 크면 GPU memory 가 모자랄 수 있다 (어림).
MITSUBA_PASS_SAMPLES = 16
# run 의 Seed s 의 pass p 는 seed s * MITSUBA_SEED_STRIDE + p 로 그린다. pass 수보다 커서 두 Seed 의 pass 가 seed 를 나누지 않는다.
MITSUBA_SEED_STRIDE = 1 << 20
WAIT_TIMEOUT = 3600


def pt_name(bounces, seed):
    return f'pt-b{bounces}-seed{seed}'


def mitsuba_name(bounces, seed):
    return f'mitsuba-b{bounces}-seed{seed}'


def mitsuba_scene_path(bounces):
    return KEPT_DIR / f'mitsuba-b{bounces}.xml'


def source_scene():
    return cornellbox.written_source(cornellbox.written_gltf(VIEWER_DIR / SCENE))


def defaults_of(root):
    return {element.get('name'): element for element in root.findall('default')}


def resolved(root, element):
    value = element.get('value')
    return defaults_of(root)[value[1:]].get('value') if value.startswith('$') else value


def named(parent, tag, name):
    found = [element for element in parent.findall(tag) if element.get('name') == name]
    if len(found) != 1:
        raise cornellbox.SceneError(f'{parent.tag} needs one {tag} {name}')
    return found[0]


def film_size(root):
    """원본 film 의 (width, height)"""
    film = root.find('sensor').find('film')
    return tuple(int(resolved(root, named(film, 'integer', name))) for name in ('width', 'height'))


def shifted_film(root):
    """원본 sensor 를 반 pixel 옮겨 그릴 (film 의 한 변 W', 시야각 fov' (degree)). 원본 film 은 정사각형이고 fov 가 x 축의 값이어야 한다."""
    sensor = root.find('sensor')
    width, height = film_size(root)
    if width != height or [element for element in sensor.findall('string') if element.get('name') == 'fov_axis']:
        raise cornellbox.SceneError('the half pixel shift needs a square film and a fov without fov_axis')
    fov = float(resolved(root, named(sensor, 'float', 'fov')))
    padded = 2 * width + 2
    return padded, math.degrees(2.0 * math.atan(math.tan(math.radians(fov) / 2.0) * padded / (2 * width)))


def mitsuba_scene(bounces):
    """원본 scene 에서 docstring 의 네 가지만 바꾼 XML tree"""
    tree = ElementTree.parse(source_scene())
    root = tree.getroot()
    padded, fov = shifted_film(root)
    defaults = defaults_of(root)
    for name, value in [('resx', padded), ('resy', padded), ('spp', MITSUBA_PASS_SAMPLES), ('max_depth', bounces + 1)]:
        defaults[name].set('value', str(value))
    sensor = root.find('sensor')
    named(sensor, 'float', 'fov').set('value', repr(fov))
    sensor.find('film').find('rfilter').set('type', 'box')
    return tree


def xml_differences(original, changed):
    """두 XML element 의 다른 점을 (경로, attribute, 원본 값, 바꾼 값) 으로. 자식의 수나 tag 가 다르면 attribute 가 'structure' 다."""
    differences = []

    def walk(a, b, path):
        if a.tag != b.tag or len(a) != len(b):
            differences.append((path, 'structure', a.tag, b.tag))
            return
        for key in sorted(set(a.attrib) | set(b.attrib)):
            if a.get(key) != b.get(key):
                differences.append((path, key, a.get(key), b.get(key)))
        for index, (child_a, child_b) in enumerate(zip(a, b)):
            walk(child_a, child_b, f'{path}/{child_a.tag}[{child_a.get("name") or child_a.get("id") or child_a.get("type") or index}]')
    walk(original, changed, original.tag)
    return differences


def expected_differences(original, bounces):
    padded, fov = shifted_film(original)
    defaults = defaults_of(original)
    expected = [(f'scene/default[{name}]', 'value', defaults[name].get('value'), str(value))
                for name, value in [('resx', padded), ('resy', padded), ('spp', MITSUBA_PASS_SAMPLES), ('max_depth', bounces + 1)]]
    sensor = original.find('sensor')
    fov_element = named(sensor, 'float', 'fov')
    expected.append((f'scene/sensor[{sensor.get("type")}]/float[fov]', 'value', fov_element.get('value'), repr(fov)))
    film = sensor.find('film')
    rfilter = film.find('rfilter')
    expected.append((f'scene/sensor[{sensor.get("type")}]/film[{film.get("type")}]/rfilter[{rfilter.get("type")}]', 'type', rfilter.get('type'), 'box'))
    # 원본의 max_depth 65 처럼 바꿀 값이 원본과 같으면 다른 점이 아니다.
    return [difference for difference in expected if difference[2] != difference[3]]


# --- accumulate ---------------------------------------------------------------------------

def state_for_validity(state):
    """[X4] 와 [X6] 이 보는 PT run 의 state"""
    camera_keys = ('x', 'y', 'z', 'heading', 'pitch', 'vertical_fov', 'forward', 'near', 'far')
    return {'scene': state['ui']['scene'], 'camera': {key: state['camera'][key] for key in camera_keys},
            'light_intensity': state['light']['intensity'], 'sky_intensity': state['sky']['intensity'],
            'path_tracer': path_tracer_settings(state['settings']['Engine Tuning']), 'samples': state['pt']['samples'], 'seconds': state['pt']['seconds']}


def accumulate_path_tracer(manifest, no_build):
    width, height = film_size(ElementTree.parse(source_scene()).getroot())
    instance = start_instance('Profile', scene=SCENE, build_first=not no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok(f'window client {width}x{height}')
            session.ask_ok('ui hide')
            manifest['machine'] = describe_machine(session)
            for bounces in BOUNCES.values():
                for seed in SEEDS:
                    set_path_tracer(session, COMPARISON | {'Max Bounces': bounces, 'Seed': seed, 'Target Samples': SAMPLES})
                    session.ask_ok('pt reset')
                    session.ask_ok(f'pt wait samples={SAMPLES}', timeout=WAIT_TIMEOUT)
                    name = pt_name(bounces, seed)
                    reply = session.ask_ok(f'capture pt {shots_dir(instance) / name}')
                    state = json.loads(Path(reply['state']).read_text(encoding='utf-8'))
                    shutil.move(reply['path'], KEPT_DIR / f'{name}.dds')
                    manifest['runs'][name] = {'file': f'{name}.dds', 'line': reply['line'], 'state': state_for_validity(state)}
                    print(f'{name}: {SAMPLES} spp in {float(state["pt"]["seconds"]):.1f} s', flush=True)
        finally:
            session.close()
    finally:
        stopped = stop_instance(instance)
    manifest['app'] = {'result': stopped['result'], 'exit_reason': stopped['exit_reason']}


def accumulate_mitsuba(manifest):
    import mitsuba
    mitsuba.set_variant(MITSUBA_VARIANT)
    manifest['mitsuba'] = {'version': mitsuba.__version__, 'variant': MITSUBA_VARIANT}
    passes = SAMPLES // 4 // MITSUBA_PASS_SAMPLES
    for bounces in BOUNCES.values():
        path = mitsuba_scene_path(bounces)
        mitsuba_scene(bounces).write(path, encoding='utf-8')
        scene = mitsuba.load_file(str(path))
        for seed in SEEDS:
            start = time.monotonic()
            total = None
            for index in range(passes):
                image = np.array(mitsuba.render(scene, spp=MITSUBA_PASS_SAMPLES, seed=seed * MITSUBA_SEED_STRIDE + index), dtype=np.float64)
                total = image if total is None else total + image
            seconds = time.monotonic() - start
            padded = total / passes
            height, width = (padded.shape[0] - 2) // 2, (padded.shape[1] - 2) // 2
            averaged = padded[:2 * height, :2 * width].reshape(height, 2, width, 2, 3).mean(axis=(1, 3)).astype(np.float32)
            name = mitsuba_name(bounces, seed)
            np.save(KEPT_DIR / f'{name}.npy', averaged)
            manifest['runs'][name] = {'file': f'{name}.npy', 'scene': path.name, 'passes': passes, 'pass_samples': MITSUBA_PASS_SAMPLES,
                                      'first_seed': seed * MITSUBA_SEED_STRIDE, 'seconds': seconds}
            print(f'{name}: {SAMPLES} samples per pixel in {seconds:.1f} s', flush=True)


def read_run(manifest, name):
    file = KEPT_DIR / manifest['runs'][name]['file']
    return read_dds_rgba32f(file) if file.suffix == '.dds' else np.load(file)


def seed_mean(manifest, name_of, bounces):
    return sum(np.asarray(read_run(manifest, name_of(bounces, seed))[..., :3], dtype=np.float64) for seed in SEEDS) / len(SEEDS)


def keep_previews(manifest):
    """[X5] 의 PNG"""
    from PIL import Image
    for bounces in BOUNCES.values():
        path_tracer, mitsuba = seed_mean(manifest, pt_name, bounces), seed_mean(manifest, mitsuba_name, bounces)
        for label, image in [('pt', path_tracer), ('mitsuba', mitsuba), ('difference', np.abs(path_tracer - mitsuba))]:
            Image.fromarray(tone_map_for_view(image, PREVIEW_EXPOSURE)).save(KEPT_DIR / f'b{bounces}-{label}.png')


def command_accumulate(args):
    if KEPT_DIR.exists():
        shutil.rmtree(KEPT_DIR)
    KEPT_DIR.mkdir(parents=True)
    manifest = {'code': code_version(), 'samples': SAMPLES, 'runs': {}}
    accumulate_path_tracer(manifest, args.no_build)
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    accumulate_mitsuba(manifest)
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    keep_previews(manifest)
    print(f'app: {manifest["app"]["result"]}, exit reason={manifest["app"]["exit_reason"]}')
    print(f'kept: {KEPT_DIR}')
    return 0 if manifest['app']['exit_reason'] == 'normal' else 1


# --- check --------------------------------------------------------------------------------

def same_float32(a, b):
    return bool(np.float32(a) == np.float32(b))


def path_tracer_problems(manifest, scene_camera, size):
    problems = []
    for bounces in BOUNCES.values():
        for seed in SEEDS:
            name = pt_name(bounces, seed)
            run = manifest['runs'][name]
            state = run['state']
            alpha = read_run(manifest, name)[..., 3]
            if not np.all(alpha == SAMPLES) or int(state['samples']) != SAMPLES:
                problems.append(f'{name}: a is not {SAMPLES} everywhere, or {state["samples"]} samples were dispatched')
            if (state['scene']['width'], state['scene']['height']) != size:
                problems.append(f'{name}: scene {state["scene"]}')
            camera = [state['camera'][key] for key in ('x', 'y', 'z', 'heading', 'pitch', 'vertical_fov')]
            if not all(same_float32(a, b) for a, b in zip(camera, scene_camera)):
                problems.append(f'{name}: camera {camera} vs scene file {scene_camera}')
            if not same_float32(state['light_intensity'], 0) or not same_float32(state['sky_intensity'], 0):
                problems.append(f'{name}: light {state["light_intensity"]}, sky {state["sky_intensity"]}')
            expected = COMPARISON | {'Max Bounces': bounces, 'Seed': seed, 'Target Samples': SAMPLES}
            differing = {key: state['path_tracer'].get(key) for key, value in expected.items() if state['path_tracer'].get(key) != str(value)}
            if differing:
                problems.append(f'{name}: path tracer settings {differing}')
    return problems


def mitsuba_problems(manifest, original):
    problems = []
    for bounces in BOUNCES.values():
        changed = ElementTree.parse(mitsuba_scene_path(bounces)).getroot()
        differences, expected = xml_differences(original, changed), expected_differences(original, bounces)
        if sorted(differences) != sorted(expected):
            problems.append(f'b{bounces}: the scene differs from the original in {sorted(set(differences) - set(expected))} and lacks '
                            f'{sorted(set(expected) - set(differences))}')
        for seed in SEEDS:
            name = mitsuba_name(bounces, seed)
            run = manifest['runs'][name]
            image = read_run(manifest, name)
            if not np.all(np.isfinite(image)) or np.any(image < 0.0):
                problems.append(f'{name}: not finite or below 0')
            if run['passes'] * run['pass_samples'] * 4 != SAMPLES or run['first_seed'] != seed * MITSUBA_SEED_STRIDE:
                problems.append(f'{name}: {run["passes"]} passes of {run["pass_samples"]} from seed {run["first_seed"]}')
    return problems


def check_validity(check, manifest):
    """[X4]"""
    original = ElementTree.parse(source_scene()).getroot()
    size = film_size(original)
    camera = read_scene_file(SCENE)[CAMERA_OBJECT]
    scene_camera = camera['Position'].split(',') + [camera['Heading'], camera['Pitch'], camera['Vertical FOV']]
    problems = path_tracer_problems(manifest, scene_camera, size) + mitsuba_problems(manifest, original)
    app = manifest['app']
    if app != {'result': 'quit', 'exit_reason': 'normal'}:
        problems.append(f'app {app}')
    return check('[X4] the runs are valid: the PT exports have every sample, the Mitsuba images are finite and 0 or more, the Mitsuba scene is '
                 'the original but the four changes with the half pixel shift of its film, the PT ran in the conditions and the app quit '
                 'normally', not problems, '; '.join(problems[:4]) or f'{len(manifest["runs"])} runs')


def compare(manifest):
    results = {}
    for criterion, bounces in BOUNCES.items():
        path_tracer = [read_run(manifest, pt_name(bounces, seed)) for seed in SEEDS]
        mitsuba = [read_run(manifest, mitsuba_name(bounces, seed)) for seed in SEEDS]
        results[criterion] = (bias(*path_tracer, *mitsuba), relative_noise(*path_tracer), relative_noise(*mitsuba))
    return results


def check_comparison(check, results):
    """[X1] ~ [X3]"""
    for criterion, (result, path_tracer_noise, mitsuba_noise) in results.items():
        bounces = BOUNCES[criterion]
        check(f'[{criterion}] Max Bounces {bounces} against Mitsuba max_depth {bounces + 1}: the bounds and floors of d and b are '
              f'{BIAS_BUDGET:g} or less',
              max(result.difference_bound, result.difference_floor, result.rms_bound, result.rms_floor) <= BIAS_BUDGET,
              f'd {result.difference:.3e} (bound {result.difference_bound:.3e}, floor {result.difference_floor:.3e}), '
              f'b {result.rms:.3e} (bound {result.rms_bound:.3e}, floor {result.rms_floor:.3e}); '
              f'noise of a run: PT {path_tracer_noise:.3e}, Mitsuba {mitsuba_noise:.3e}')


def shape_interiors(manifest):
    """[X6] 의 shape 마다 안쪽 pixel 의 mask. PT run 의 camera 와 scene 크기로 Mitsuba scene 의 면을 본다."""
    faces = cornellbox.world_faces(cornellbox.read(source_scene()), float(read_scene_file(SCENE)['Model']['Scale']))
    state = manifest['runs'][pt_name(BOUNCES['X1'], SEEDS[0])]['state']
    center = seen_faces(state['camera'], state['scene'], faces)
    interior = np.all([seen_faces(state['camera'], state['scene'], faces, offset) == center
                       for offset in PIXEL_EDGE_OFFSETS[1:] + PIXEL_CORNER_OFFSETS], axis=0)
    return {shape: interior & np.isin(center, [index for index, face in enumerate(faces) if face.shape == shape])
            for shape in sorted({face.shape for face in faces})}


def compare_shapes(manifest, interiors):
    """(깊이, shape) 마다 안쪽 pixel 의 imagecompare.bias. 두 renderer 가 모두 0 이면 'zero', PT 의 합만 0 이면 'pt zero' 다."""
    results = {}
    for bounces in BOUNCES.values():
        path_tracer = [read_run(manifest, pt_name(bounces, seed))[..., :3] for seed in SEEDS]
        mitsuba = [read_run(manifest, mitsuba_name(bounces, seed)) for seed in SEEDS]
        for shape, interior in interiors.items():
            pixels = [image[interior] for image in path_tracer + mitsuba]
            if not any(np.any(image != 0.0) for image in pixels):
                results[(bounces, shape)] = 'zero'
            elif not any(np.any(image != 0.0) for image in pixels[:len(SEEDS)]):
                results[(bounces, shape)] = 'pt zero'
            else:
                results[(bounces, shape)] = bias(*pixels)
    return results


def check_shapes(check, interiors, results):
    """[X6]"""
    judged = {shape: [] for shape in interiors}
    for bounces in BOUNCES.values():
        failed, unjudged, passed = [], [], []
        for shape, interior in interiors.items():
            result = results[(bounces, shape)]
            if result == 'zero':
                judged[shape].append(bounces)
                passed.append(f'{shape} 0 in both')
            elif result == 'pt zero':
                judged[shape].append(bounces)
                failed.append(f'{shape}: PT is 0 where Mitsuba is not')
            elif not result.difference_floor <= BIAS_BUDGET:
                unjudged.append(f'{shape} (floor {result.difference_floor:.1e})')
            else:
                judged[shape].append(bounces)
                text = (f'{shape} d {result.difference:+.2e} (bound {result.difference_bound:.2e}, floor {result.difference_floor:.1e}, '
                        f'{int(interior.sum())} pixels)')
                (failed if result.difference_bound > BIAS_BUDGET else passed).append(text)
        check(f'[X6] Max Bounces {bounces}: the d bound and floor of each shape in its interior pixels are {BIAS_BUDGET:g} or less',
              not failed, '; '.join(failed + passed) + (f'; not judged: {", ".join(unjudged)}' if unjudged else ''))
    never = [shape for shape, depths in judged.items() if not depths]
    check('[X6] every shape is judged at one depth or more', not never, ', '.join(never) or
          ', '.join(f'{shape} at {depths}' for shape, depths in judged.items()))


def record(manifest, results):
    today = date.today().isoformat()
    mitsuba = f'{manifest["mitsuba"]["version"]} {manifest["mitsuba"]["variant"]}'
    for criterion, (result, path_tracer_noise, mitsuba_noise) in results.items():
        bounces = BOUNCES[criterion]
        append_record_row(RECORD_SECTION, [
            today, manifest['code'], *manifest['machine'], f'[{criterion}]', str(bounces), str(bounces + 1), mitsuba, str(SAMPLES),
            f'{path_tracer_noise:.2e}', f'{mitsuba_noise:.2e}', f'{result.difference:.2e}', f'{result.difference_bound:.2e}',
            f'{result.difference_floor:.2e}', f'{result.rms:.2e}', f'{result.rms_bound:.2e}', f'{result.rms_floor:.2e}'])


def command_check(args):
    check = Checks()
    try:
        manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
        valid = check_validity(check, manifest)
        results = compare(manifest)
        check_comparison(check, results)
        interiors = shape_interiors(manifest)
        check_shapes(check, interiors, compare_shapes(manifest, interiors))
    except (OSError, KeyError, ImageFileError, cornellbox.SceneError) as error:
        check('every run of accumulate is kept', False, f'{error!r}; run accumulate')
        valid, results = False, {}
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    if args.record and valid:
        record(manifest, results)
        print('recorded in docs/reference-path-tracer-measurements.md')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('accumulate', help='accumulate the PT in Profile and the original scene in Mitsuba and keep them')
    command.add_argument('--no-build', action='store_true', help='use the exe as it is')
    command.set_defaults(handler=command_accumulate)
    command = commands.add_parser('check', help='judge the criteria from what accumulate kept')
    command.add_argument('--record', action='store_true', help='append the measurements to the record when the runs are valid')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except HarnessError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
