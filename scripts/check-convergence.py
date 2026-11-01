"""PT 가 측정 scene 에서 bias 없이 수렴하는지 판정하고, 정답 (ground truth) 의 spp 를 정한다.

  python scripts/check-convergence.py accumulate [--group G ...]   # Profile 로 띄워 아래 run 을 쌓고 export 를 KEPT_DIR 에 남긴다
  python scripts/check-convergence.py check [--record]            # KEPT_DIR 의 export 로 아래 기준을 판정한다

번호는 작업 12 의 완료 기준의 번호다. accumulate 는 app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. group 을
고르지 않으면 GROUPS 를 차례로 쌓고, run 하나를 끝낼 때마다 MANIFEST 에 남긴다. check 는 app 을 띄우지 않고, 하나라도 실패하면
exit 1 이다. 이 script 밖에서 보는 기준:
  [C7] d, b 와 그 상한의 식: imagecompare.py selftest

조건: 측정 scene (viewer.measurement_scenes) 마다 scene file 의 camera, directional light, Sky 와 MEASUREMENT (Output Indirect
Irradiance, Max Bounces 64, Russian roulette on, Geometric Normals off). Sky 는 pathtracerchecks.BASE 의 값으로 바꾸지 않는다. 사용자가
측정 scene 의 구도를 잡을 때와 발표용 image 를 만들 때 scene file 의 값을 쓰기 때문이다. Indirect Irradiance 는 DDGI 가 비교할 양이다.
정답을 만들 Profile 에서 1920x1080 으로 쌓는다. export 의 rgb 는 E (lux) 의 평균, a 는 더한 sample 수다. X_s(N) 은 Seed s 로 N spp
쌓은 export 다. 한 run 에서 Target Samples 만 올려 2 의 거듭제곱마다 export 하며, Target Samples 를 올려도 누적은 처음부터 다시
시작하지 않는다 (check-lambert-path.py 의 [9]).

accumulate 는 run 마다 scene 을 다시 열고, 연 뒤의 state 에서 읽은 scene file 의 경로와 camera (세로 FOV 를 담는다), light, Sky 를
MANIFEST 의 run 에 남긴다. check 는 scene file 의 경로와 camera, light, Sky 가 없거나 지금 scene file 의 값과 다른 run 을 쓰지 않고
FAIL 로 알린다. 쓰지 않은 run 은 다시 accumulate 한다. 세로 FOV 를 남기기 전의 run 은 45° 로 쌓은 것으로 본다 (UNRECORDED_VERTICAL_FOV).

  e(N) = RMSE(X0(N), X1(N)) / (sqrt(2) rms(Xm(N)))   한 run 의 상대 noise. Xm 은 두 Seed 의 평균이다.

예산: 정답 한 run 의 e 는 NOISE_BUDGET, bias 원인 하나는 BIAS_BUDGET 이하다. check-gltf-brdf.py 의 [B5] 가 작업 12 의 정답 오차를 1%
로 보고 허용 오차를 그 1/5 로 정했으므로 같은 값을 쓴다. d, b 와 그 상한, floor 는 imagecompare.py 의 bias 다. floor 가 예산보다 크면
그 spp 로는 차가 예산 아래인지 알 수 없으므로 FAIL 이다.

Shading glTF 는 흰 점이 많은 측정 scene 에서 시간 안에 NOISE_BUDGET 에 이르지 못한다. 측정 scene 에서 16384 spp 의 e 가
sponza-color-bleeding 2.3%, sponza-fireflies 9.9%, sponza-overhead-light 2.5% 였고, squared error 의 8.5%, 65%, 66% 가 상위 0.1% pixel
에 있었다. sponza-fireflies 는 e 가 N^-0.5 로 줄어도 1% 에 이르려면 2^21 spp 가 필요하다 (어림). 흰 점은 로드맵의 뒤로 미룬 일에
있다. 그래서 판정은 Lambert 로 하고, glTF 는 e(N) 과 흰 점의 몫만 잰다.

  [C1] Lambert 의 측정 scene 마다 SLOPE_FIT_FROM 부터 끝 spp 까지 log e 대 log N 을 fit 한 기울기가 -0.5 ± SLOPE_TOLERANCE 이고, 끝
       spp 의 1/4 부터 끝까지의 기울기가 -0.5 ± LAST_SLOPE_TOLERANCE 다. 같은 pixel 의 sample 끼리 상관이 있거나 sample 의 분산이
       무한에 가까우면 기울기가 완만해진다. 큰 sample 이 드물면 작은 N 의 e 가 작게 나오므로 끝 구간을 따로 본다.
  [C2] Lambert 의 측정 scene 마다 e(N) <= NOISE_BUDGET 인 가장 작은 2 의 거듭제곱이 있고, scene 들의 거듭제곱 중 최대인 N_gt 가
       PathTracer::Settings 의 targetSamples 기본값이다. 외삽하지 않고 잰 e 로 정한다. SCENES_WITHOUT_GROUND_TRUTH 의 scene 은 N_gt 를
       정하지 않고 e 만 출력한다.
  [C3] Max Bounces: 측정 scene 마다 Lambert 를 Max Bounces 64 와 BOUNCES_REFERENCE 로 BIAS_SAMPLES 만큼 쌓은 d, b 의 상한이
       BIAS_BUDGET 이하다. 두 쪽은 Seed 가 같아서 64 번 안에 끝나는 path 가 같은 sample 을 낸다. 그래서 차에는 64 번을 넘는 path 의
       몫만 남고, 64 번을 넘는 path 가 없으면 d, b 와 상한이 0 이다. 다만 Shading Lambert 의 export 는 같은 build 에서도 실행마다 몇 ulp
       다를 수 있으므로 (로드맵의 뒤로 미룬 일) 실행마다 다른 몫이 d, b 에 남는다.
  [C4] Russian roulette: 측정 scene 마다 Lambert 를 Max Bounces ROULETTE_BOUNCES 에서 on 과 off 로 BIAS_SAMPLES 만큼 쌓은 d, b 의
       상한이 BIAS_BUDGET 이하다. Max Bounces 를 줄여 off 의 시간을 줄인다. roulette 는 PathTracerCS.hlsl 의
       kFirstRussianRouletteBounce 번째 반사부터 걸리므로 그 뒤의 반사에서 본다.
  [C5] float32 누적: N_gt 를 정한 측정 scene 의 X0(N_gt), X1(N_gt) 와, ACCUMULATION_SEEDS 로 N_gt / ACCUMULATION_RUNS 씩 쌓아 float64
       로 평균한 B0 의 d, b 상한이 BIAS_BUDGET 이하다. PathTracerCS.hlsl 은 평균을 float32 로 sample 마다 고쳐 쓰므로, N 이 크면
       반올림이 한쪽으로 쌓일 수 있다. B0 는 sample 수의 합이 같으므로 noise 분산도 같아서, imagecompare 의 B1 이 없는 식을 쓴다.
       N_gt 가 없으면 판정하지 못하므로 FAIL 이다.
  [C6] GROUPS 의 run 이 남긴 모든 export 에서 pixel 의 a 가 0 이나 dispatch 한 sample 수이고, a 가 0 인 pixel 은 rgb 가 0 이며
       a 가 0 인 pixel 의 자리가 같은 측정 scene 의 모든 export 에서 같다. NaN 이나 Inf 라서 버린 sample 이 없다는 뜻이다.

check --record 는 모두 통과하면 수렴과 bias 를 RECORD 에 덧붙인다. accumulate 는 발표에 쓰도록 slope 의 끝 spp 를 보기용 PNG 로도 남긴다.
"""

import argparse
import json
import math
import os
import re
import shutil
import sys
from collections import namedtuple
from datetime import date
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks
from imagecompare import bias
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import BASE, SKY_COLOR, SKY_RADIANCE, append_record_row, code_version, describe_machine, set_path_tracer
from viewer import (ENGINE_DIR, HARNESS_DIR, PREVIEW_EXPOSURE, REPO_ROOT, VIEWER_DIR, HarnessError, Session, default_vertical_fov, make_view,
                    measurement_scenes, read_scene_file, shots_dir, start_instance, stop_instance)

KEPT_DIR = HARNESS_DIR / 'kept' / 'convergence'
MANIFEST = KEPT_DIR / 'manifest.json'
# MANIFEST 의 run 이 camera 의 vertical_fov 를 남기기 전에는 scene file 이 세로 FOV 를 정할 수 없었고, app 은 늘 45° 로 그렸다.
UNRECORDED_VERTICAL_FOV = math.pi / 4
SETTINGS_HEADER = ENGINE_DIR / 'Model' / 'PathTracer' / 'PathTracer.h'
CONVERGENCE_RECORD_SECTION = '## 작업 12: Indirect Irradiance 의 수렴'
BIAS_RECORD_SECTION = '## 작업 12: Indirect Irradiance 의 bias'

# Samples per Frame 는 image 를 바꾸지 않는다 (check-path-tracer-export.py 의 [S1]). 1 spp 씩 쌓으면 VSync 가 frame 을 기다리게 하므로
# 한 frame 에 여럿을 쌓는다. Sky 는 scene file 의 값을 쓰므로 BASE 에서 뺀다.
MEASUREMENT = {key: value for key, value in BASE.items() if key not in (SKY_COLOR, SKY_RADIANCE)} | {
    'Output': 'Indirect Irradiance', 'Geometric Normals': 0, 'Max Bounces': 64, 'Russian Roulette': 1, 'Samples per Frame': 16}
LAMBERT = {'Shading': 'Lambert'}
GLTF = {'Shading': 'glTF'}

NOISE_BUDGET = 0.01
BIAS_BUDGET = 0.002
# N_gt 를 정하지 않고 e 만 재는 측정 scene. sponza-fireflies 는 noise 가 커서 Lambert 가 NOISE_BUDGET 에 이르려면 약 2^19 spp, 한 Seed 에
# 약 67 분이 걸린다 (e(1024) 로 한 어림). 사용자가 noise 를 줄인 뒤에 정답의 구도로 쓰기로 했다.
SCENES_WITHOUT_GROUND_TRUTH = ['sponza-fireflies']
FIRST_EXPORT = 16
# Lambert 의 끝 spp 는 1024 spp 의 e (sponza-color-bleeding 8.6%, sponza-fireflies 17.0%, sponza-overhead-light 5.9%) 로 정했다.
# e(N) ≈ e(1024) sqrt(1024 / N) 이 1% 아래로 내려갈 가장 작은 2 의 거듭제곱 N 을 고르고, 예측한 e(N) 이 1% 의 0.9 배를 넘으면 2N 을
# 쓰며, 2^18 을 넘지 않는다. sponza-fireflies 는 예측한 e(2^18) 이 1.06% 라서 상한까지만 쌓는다.
LAMBERT_SLOPE_SAMPLES = {'sponza-color-bleeding': 1 << 17, 'sponza-fireflies': 1 << 18, 'sponza-overhead-light': 1 << 16}
# glTF 는 1% 에 이르지 못하므로 기울기를 볼 만큼만 쌓는다.
GLTF_SLOPE_SAMPLES = 1 << 14
SLOPE_FIT_FROM = 256
SLOPE_TOLERANCE = 0.05
LAST_SLOPE_TOLERANCE = 0.1
# imagecompare 의 b 의 floor 는 두 run 의 차의 상대 noise 에 비례한다. 1024 spp 에서는 BIAS_BUDGET 보다 커서 판정할 수 없다 (어림).
BIAS_SAMPLES = 1 << 14
BOUNCES_REFERENCE = 4096
ROULETTE_BOUNCES = 8
ACCUMULATION_RUNS = 16
ACCUMULATION_SEEDS = list(range(2, 2 + ACCUMULATION_RUNS))
# 흰 점의 몫을 보는 pixel 의 비율
TOP_PIXELS = 1e-3
PROFILE_FRAMES = 8
# 가장 긴 wait 는 sponza-overhead-light 의 Russian roulette off 를 BIAS_SAMPLES 까지 쌓는 것으로, 측정 scene 으로 처음 판정할 때 약 17 분 걸렸다.
WAIT_TIMEOUT = 3 * 3600

Run = namedtuple('Run', 'group name scene settings exports')


def powers_of_two(last):
    return [1 << exponent for exponent in range(FIRST_EXPORT.bit_length() - 1, last.bit_length())]


def slope_name(scene, shading, seed):
    return f'{scene}-{shading.lower()}-seed{seed}'


def bounces_name(scene, seed):
    return f'{scene}-lambert-b{BOUNCES_REFERENCE}-seed{seed}'


def roulette_name(scene, roulette, seed):
    return f'{scene}-lambert-b{ROULETTE_BOUNCES}-rr{roulette}-seed{seed}'


def accumulation_name(scene, seed):
    return f'{scene}-lambert-seed{seed}'


def slope_samples(scene, shading):
    if shading == 'glTF':
        return GLTF_SLOPE_SAMPLES
    if scene not in LAMBERT_SLOPE_SAMPLES:
        raise HarnessError(f'LAMBERT_SLOPE_SAMPLES has no end spp for the measurement scene {scene}')
    return LAMBERT_SLOPE_SAMPLES[scene]


def slope_runs():
    # glTF 는 끝 spp 가 작아서 Lambert 보다 먼저 쌓는다.
    return [Run('slope', slope_name(scene, shading, seed), scene, MEASUREMENT | {'Shading': shading, 'Seed': seed},
                powers_of_two(slope_samples(scene, shading)))
            for shading in ('glTF', 'Lambert') for scene in measurement_scenes() for seed in (0, 1)]


def bounces_runs():
    return [Run('bounces', bounces_name(scene, seed), scene, MEASUREMENT | LAMBERT | {'Max Bounces': BOUNCES_REFERENCE, 'Seed': seed},
                [BIAS_SAMPLES]) for scene in measurement_scenes() for seed in (0, 1)]


def roulette_runs():
    # off 는 1 spp 가 on 보다 훨씬 길어서 한 frame 의 dispatch 를 줄인다.
    return [Run('roulette', roulette_name(scene, roulette, seed), scene,
                MEASUREMENT | LAMBERT | {'Max Bounces': ROULETTE_BOUNCES, 'Russian Roulette': roulette, 'Seed': seed,
                                         'Samples per Frame': 16 if roulette else 4}, [BIAS_SAMPLES])
            for scene in measurement_scenes() for roulette in (1, 0) for seed in (0, 1)]


def accumulation_runs(scene, samples):
    return [Run('accumulation', accumulation_name(scene, seed), scene, MEASUREMENT | LAMBERT | {'Seed': seed}, [samples // ACCUMULATION_RUNS])
            for seed in ACCUMULATION_SEEDS]


def accumulation_runs_of_kept_slopes(manifest):
    """N_gt 를 정한 측정 scene 의 [C5] run. N_gt 와 [C5] 를 쌓을 scene 은 MANIFEST 에 쌓은 Lambert 의 slope run 이 정한다."""
    kept, stale = usable_runs(manifest)
    if stale:
        raise HarnessError(f'{", ".join(stale)} in {MANIFEST} were not accumulated in the scene files as they are now; accumulate them again')
    try:
        scene, samples = ground_truth({scene: convergence(kept, scene, 'Lambert')[0] for scene in ground_truth_scenes()})
    except KeyError as error:
        raise HarnessError(f'{MANIFEST} misses {error}; accumulate the slope group first')
    if samples is None:
        raise HarnessError(f'the slope runs in {MANIFEST} do not reach e <= {NOISE_BUDGET:g}, so [C5] has no N_gt to accumulate')
    return accumulation_runs(scene, samples)


GROUPS = {'slope': lambda manifest: slope_runs(), 'bounces': lambda manifest: bounces_runs(), 'roulette': lambda manifest: roulette_runs(),
          'accumulation': accumulation_runs_of_kept_slopes}


def run_scenes():
    """GROUPS 의 run 의 이름과 run 을 쌓는 측정 scene. accumulation 은 N_gt 를 정한 scene 에서만 쌓지만, N_gt 를 정하는
    scene 은 쌓은 slope run 에 따라 달라지므로 ground_truth_scenes() 의 모든 scene 의 이름을 둔다."""
    runs = {run.name: run.scene for run in slope_runs() + bounces_runs() + roulette_runs()}
    return runs | {accumulation_name(scene, seed): scene for scene in ground_truth_scenes() for seed in ACCUMULATION_SEEDS}


def read_manifest():
    return json.loads(MANIFEST.read_text(encoding='utf-8')) if MANIFEST.exists() else {'runs': {}}


def write_manifest(manifest):
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


# --- 측정 scene 의 값 -----------------------------------------------------------------------

def repo_path(scene):
    """MiniEngine/ModelViewer 기준 scene file 경로를 repo 기준 경로로."""
    return Path(os.path.normpath(VIEWER_DIR / scene)).relative_to(REPO_ROOT).as_posix()


def scene_values_in_state(state):
    """state 의 camera, light, Sky 중 scene file 이 정하고 PT 의 image 를 바꾸는 값. MANIFEST 의 run 에 돌려준 dict 그대로 남긴다."""
    camera, light, sky = state['camera'], state['light'], state['sky']
    return {'camera': {key: camera[key] for key in ('x', 'y', 'z', 'heading', 'pitch', 'vertical_fov')},
            'light': {key: light[key] for key in ('direction', 'color', 'intensity')},
            'sky': {key: sky[key] for key in ('color', 'intensity')}}


def scene_values_in_file(scene):
    """scene file 의 값을 scene_values_in_state 의 모양으로."""
    ini = read_scene_file(scene)
    camera, light, sky = ini['Camera'], ini['Directional Light'], ini['Sky']

    def vector(text, keys):
        return dict(zip(keys, text.split(',')))
    return {'camera': vector(camera['Position'], 'xyz') | {'heading': camera['Heading'], 'pitch': camera['Pitch'],
                                                          'vertical_fov': camera.get('Vertical FOV', default_vertical_fov())},
            'light': {'direction': vector(light['Direction'], 'xyz'), 'color': vector(light['Color'], 'rgb'), 'intensity': light['Intensity']},
            'sky': {'color': vector(sky['Color'], 'rgb'), 'intensity': sky['Intensity']}}


def same_values(a, b):
    """모양이 같고 숫자가 float32 로 같은지. app 은 camera, light, Sky 의 값을 float32 로 갖고, state 는 float32 로 되돌아오는 자릿수로
    적는다."""
    if isinstance(a, dict) or isinstance(b, dict):
        return isinstance(a, dict) and isinstance(b, dict) and a.keys() == b.keys() and all(same_values(a[key], b[key]) for key in a)
    try:
        return bool(np.float32(a) == np.float32(b))
    except (TypeError, ValueError):
        return False


def accumulated_in_scene_file_as_now(entry, scene):
    """MANIFEST 의 run 이 지금 measurement_scenes() 의 scene 과 같은 scene file, camera, light, Sky 로 쌓은 것인지"""
    path = measurement_scenes()[scene]
    recorded = {key: entry.get(key) for key in ('camera', 'light', 'sky')}
    if isinstance(recorded['camera'], dict) and 'vertical_fov' not in recorded['camera']:
        recorded['camera'] = recorded['camera'] | {'vertical_fov': UNRECORDED_VERTICAL_FOV}
    return entry.get('scene') == repo_path(path) and same_values(recorded, scene_values_in_file(path))


def usable_runs(manifest):
    """MANIFEST 에서 GROUPS 의 run 중 지금 scene file 의 값으로 쌓은 run 만 담은 manifest 와, 쓰지 않는 run 의 이름.
    GROUPS 에 없는 run (예전 시점의 run) 은 둘 다에 넣지 않는다."""
    kept, stale = {}, []
    for name, scene in run_scenes().items():
        entry = manifest['runs'].get(name)
        if entry is None:
            continue
        if accumulated_in_scene_file_as_now(entry, scene):
            kept[name] = entry
        else:
            stale.append(name)
    return {'runs': kept}, stale


# --- accumulate ---------------------------------------------------------------------------

def accumulate_run(session, run, machine):
    """run 의 측정 scene 을 열고 처음부터 쌓으며 run.exports 의 spp 마다 export 를 KEPT_DIR 로 옮기고 manifest 의 항목을 돌려준다."""
    opened = session.ask_ok(f'scene open {measurement_scenes()[run.scene]}')['state']
    set_path_tracer(session, run.settings | {'Target Samples': run.exports[-1]})
    session.ask_ok('pt reset')
    gpu_ms = float(session.ask_ok(f'profile Path Tracer frames={PROFILE_FRAMES}', timeout=WAIT_TIMEOUT)['gpu_ms']['mean'])

    folder = KEPT_DIR / run.group
    folder.mkdir(parents=True, exist_ok=True)
    set_path_tracer(session, {'Target Samples': run.exports[0]})
    session.ask_ok('pt reset')
    exports = {}
    for samples in run.exports:
        set_path_tracer(session, {'Target Samples': samples})
        session.ask_ok(f'pt wait samples={samples}', timeout=WAIT_TIMEOUT)
        reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / f"{run.name}-{samples}"}')
        state = json.loads(Path(reply['state']).read_text(encoding='utf-8'))['pt']
        kept = folder / f'{run.name}-{samples}.dds'
        shutil.move(reply['path'], kept)
        exports[str(samples)] = {'file': str(kept.relative_to(KEPT_DIR)), 'dispatched': state['samples'], 'seconds': state['seconds'],
                                 'line': reply['line']}
        print(f'{run.name}: {samples} spp in {state["seconds"]:.1f} s', flush=True)
    return {'group': run.group, 'scene': repo_path(opened['scene']['path']), **scene_values_in_state(opened), 'settings': run.settings,
            'code': code_version(), 'machine': machine, 'gpu_ms_per_frame': gpu_ms, 'exports': exports}


def keep_previews(manifest):
    for run in slope_runs():
        if run.settings['Seed'] == 0 and run.name in manifest['runs']:
            entry = manifest['runs'][run.name]
            last = max(entry['exports'], key=int)
            dds = KEPT_DIR / entry['exports'][last]['file']
            make_view(dds, 'pt', dds.with_suffix('.png'), None, 1 << 16, PREVIEW_EXPOSURE / math.pi)


def command_accumulate(args):
    groups = args.group or list(GROUPS)
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    manifest = read_manifest()
    instance = start_instance('Profile', build_first=not args.no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            machine = describe_machine(session)
            for group in groups:
                for run in GROUPS[group](manifest):
                    manifest['runs'][run.name] = accumulate_run(session, run, machine)
                    write_manifest(manifest)
            keep_previews(manifest)
        finally:
            session.close()
    finally:
        stopped = stop_instance(instance)
    print(f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    print(f'kept: {KEPT_DIR}')
    return 0 if stopped['exit_reason'] == 'normal' else 1


# --- check --------------------------------------------------------------------------------

def read_export(manifest, name, samples):
    return read_dds_rgba32f(KEPT_DIR / manifest['runs'][name]['exports'][str(samples)]['file'])


def rgb64(image):
    return np.asarray(image[..., :3], dtype=np.float64)


def relative_noise(x0, x1, hits):
    x0, x1 = rgb64(x0)[hits], rgb64(x1)[hits]
    return float(np.sqrt(np.mean(np.square(x0 - x1)) / 2.0) / np.sqrt(np.mean(np.square((x0 + x1) / 2.0))))


def relative_noise_standard_error(x0, x1, hits):
    """relative_noise 의 표준오차. pixel 마다의 차의 제곱합을 독립 sample 로 본다. 분모의 흔들림은 분자보다 작아서 넣지 않는다."""
    squared = np.sum(np.square(rgb64(x0)[hits] - rgb64(x1)[hits]), axis=-1)
    return relative_noise(x0, x1, hits) * 0.5 * float(np.std(squared, ddof=1)) / np.sqrt(squared.size) / float(np.mean(squared))


def top_pixel_share(x0, x1, hits):
    """Seed 두 개의 차의 제곱합에서 가장 큰 TOP_PIXELS 의 pixel 이 차지하는 몫"""
    squared = np.sort(np.sum(np.square(rgb64(x0)[hits] - rgb64(x1)[hits]), axis=-1))[::-1]
    return float(np.sum(squared[:max(1, int(squared.size * TOP_PIXELS))]) / np.sum(squared))


def convergence(manifest, scene, shading):
    """N 마다의 e 와, 끝 spp 에서 흰 점의 몫"""
    names = [slope_name(scene, shading, seed) for seed in (0, 1)]
    samples = sorted(set.intersection(*(set(map(int, manifest['runs'][name]['exports'])) for name in names)))
    errors, share = {}, None
    for n in samples:
        x0, x1 = (read_export(manifest, name, n) for name in names)
        hits = x0[..., 3] > 0
        errors[n] = relative_noise(x0, x1, hits)
        if n == samples[-1]:
            share = top_pixel_share(x0, x1, hits)
    return errors, share


def fitted_slope(errors, first):
    points = [(math.log(n), math.log(e)) for n, e in errors.items() if n >= first]
    return float(np.polyfit(*zip(*points), 1)[0])


def first_within_budget(errors):
    """e <= NOISE_BUDGET 인 가장 작은 N 과 N 에서의 e. 못 이르면 None 이다."""
    return next(((n, e) for n, e in sorted(errors.items()) if e <= NOISE_BUDGET), None)


def ground_truth_scenes():
    """N_gt 를 정하는 측정 scene"""
    return [scene for scene in measurement_scenes() if scene not in SCENES_WITHOUT_GROUND_TRUTH]


def ground_truth(lambert_errors):
    """N_gt 를 정한 측정 scene 과 N_gt. lambert_errors 는 ground_truth_scenes() 의 scene 마다 Lambert 의 convergence 의 e 다. N_gt 는 scene 마다
    e <= NOISE_BUDGET 이 되는 가장 작은 N 의 최대이고, 최대인 scene 이 여럿이면 N_gt 에서 e 가 큰 scene 을 고른다. 못 이르는 scene 이
    있으면 둘 다 None 이다."""
    needed = {scene: first_within_budget(errors) for scene, errors in lambert_errors.items()}
    if any(reached is None for reached in needed.values()):
        return None, None
    scene = max(needed, key=needed.get)
    return scene, needed[scene][0]


def default_target_samples():
    match = re.search(r'uint32_t targetSamples = (\d+);', SETTINGS_HEADER.read_text(encoding='utf-8'))
    return int(match[1]) if match else None


def check_convergence(manifest, check):
    rows = {}
    for scene in measurement_scenes():
        for shading in ('Lambert', 'glTF'):
            errors, share = convergence(manifest, scene, shading)
            last = max(errors)
            slope, last_slope = fitted_slope(errors, SLOPE_FIT_FROM), fitted_slope(errors, last // 4)
            gpu_ms = manifest['runs'][slope_name(scene, shading, 0)]['gpu_ms_per_frame'] / MEASUREMENT['Samples per Frame']
            rows[scene, shading] = {'errors': errors, 'share': share, 'slope': slope, 'last_slope': last_slope, 'gpu_ms': gpu_ms}
            detail = (f'e {", ".join(f"{n}: {e:.4g}" for n, e in errors.items() if n >= SLOPE_FIT_FROM)}; slope {slope:.4f} from '
                      f'{SLOPE_FIT_FROM}, {last_slope:.4f} from {last // 4}; top {TOP_PIXELS:.1%} pixels have {share:.1%} of the squared error')
            if shading == 'Lambert':
                check(f'[C1] {scene}, {shading}: e falls as N^-0.5 within {SLOPE_TOLERANCE} from {SLOPE_FIT_FROM} spp and within '
                      f'{LAST_SLOPE_TOLERANCE} over the last quarter',
                      abs(slope + 0.5) <= SLOPE_TOLERANCE and abs(last_slope + 0.5) <= LAST_SLOPE_TOLERANCE, detail)
            else:
                print(f'MEASURED  {scene}, {shading} (not judged): {detail}', flush=True)

    scene, samples = ground_truth({scene: rows[scene, 'Lambert']['errors'] for scene in ground_truth_scenes()})
    default = default_target_samples()

    def noise(s):
        errors = rows[s, 'Lambert']['errors']
        reached = first_within_budget(errors)
        text = (f'{s}{" (does not set N_gt)" if s in SCENES_WITHOUT_GROUND_TRUTH else ""}: ' +
                (f'e({reached[0]}) {reached[1]:.4g}' if reached else f'e({max(errors)}) {errors[max(errors)]:.4g} is above the budget'))
        if samples in errors:
            x0, x1 = (read_export(manifest, slope_name(s, 'Lambert', seed), samples) for seed in (0, 1))
            hits = x0[..., 3] > 0
            text += f', e({samples}) {errors[samples]:.4g} (standard error {relative_noise_standard_error(x0, x1, hits):.2g})'
        return text

    check(f'[C2] e <= {NOISE_BUDGET:g} for Lambert in every ground truth scene at a power of two, and the largest is the default Target Samples',
          samples is not None and default == samples,
          f'N_gt {samples} set by {scene}; default Target Samples {default}; ' + '; '.join(noise(s) for s in measurement_scenes()))
    return rows, scene, samples


def judge_bias(check, label, a, b, hits):
    result = bias(*[image[hits] for image in a], *[image[hits] for image in b])
    conclusive = result.difference_floor <= BIAS_BUDGET and result.rms_floor <= BIAS_BUDGET
    check(f'{label}: the bounds of d and b are {BIAS_BUDGET:g} or less', conclusive and result.difference_bound <= BIAS_BUDGET and
          result.rms_bound <= BIAS_BUDGET,
          f'd {result.difference:.3g} (bound {result.difference_bound:.3g}, floor {result.difference_floor:.3g}), b {result.rms:.3g} '
          f'(bound {result.rms_bound:.3g}, floor {result.rms_floor:.3g})' + ('' if conclusive else '; a floor is above the budget'))
    return result


def check_bias(manifest, check, ground_truth_scene, ground_truth_samples):
    """판정한 bias 를 (기준, scene, 비교, spp, imagecompare.bias 의 결과) 의 list 로 돌려준다."""
    results = []

    def exports(names, samples):
        return [read_export(manifest, name, samples) for name in names]

    for scene in measurement_scenes():
        reference = exports([slope_name(scene, 'Lambert', seed) for seed in (0, 1)], BIAS_SAMPLES)
        hits = reference[0][..., 3] > 0
        truncated = exports([bounces_name(scene, seed) for seed in (0, 1)], BIAS_SAMPLES)
        results.append(('C3', scene, f'Max Bounces 64 대 {BOUNCES_REFERENCE}', BIAS_SAMPLES,
                        judge_bias(check, f'[C3] {scene}, Lambert, Max Bounces 64 against {BOUNCES_REFERENCE} at {BIAS_SAMPLES} spp', reference,
                                   truncated, hits)))
        on, off = (exports([roulette_name(scene, roulette, seed) for seed in (0, 1)], BIAS_SAMPLES) for roulette in (1, 0))
        results.append(('C4', scene, f'Max Bounces {ROULETTE_BOUNCES} 의 Russian roulette on 대 off', BIAS_SAMPLES,
                        judge_bias(check, f'[C4] {scene}, Lambert, Max Bounces {ROULETTE_BOUNCES}, Russian roulette on against off at '
                                          f'{BIAS_SAMPLES} spp', on, off, hits)))

    if ground_truth_samples is None:
        check(f'[C5] Lambert, one run of N_gt spp against the float64 mean of {ACCUMULATION_RUNS} runs', False, 'no N_gt; see [C2]')
        return results
    long_runs = exports([slope_name(ground_truth_scene, 'Lambert', seed) for seed in (0, 1)], ground_truth_samples)
    hits = long_runs[0][..., 3] > 0
    short = ground_truth_samples // ACCUMULATION_RUNS
    combined = np.zeros(long_runs[0].shape[:2] + (3,))
    for seed in ACCUMULATION_SEEDS:
        combined += rgb64(read_export(manifest, accumulation_name(ground_truth_scene, seed), short))
    combined /= ACCUMULATION_RUNS
    results.append(('C5', ground_truth_scene, f'{ground_truth_samples} spp 한 run 대 {short} spp {ACCUMULATION_RUNS} run 의 float64 평균',
                    ground_truth_samples,
                    judge_bias(check, f'[C5] {ground_truth_scene}, Lambert, one run of {ground_truth_samples} spp against the float64 mean '
                                      f'of {ACCUMULATION_RUNS} runs of {short}', long_runs, [combined], hits)))
    return results


def check_counts(manifest, check):
    wrong, zero_masks = [], {}
    for name, entry in manifest['runs'].items():
        for samples, export in entry['exports'].items():
            image = read_dds_rgba32f(KEPT_DIR / export['file'])
            added = image[..., 3]
            empty = added == 0
            short = int(np.count_nonzero(~empty & (added != export['dispatched'])))
            lit_empty = int(np.count_nonzero(empty & np.any(image[..., :3] != 0, axis=-1)))
            reference = zero_masks.setdefault(entry['scene'], empty)
            moved = int(np.count_nonzero(reference != empty))
            if short or lit_empty or moved:
                wrong.append(f'{name} at {samples}: {short} pixels short, {lit_empty} empty pixels lit, {moved} empty pixels moved')
    exports = sum(len(entry['exports']) for entry in manifest['runs'].values())
    check('[C6] every pixel of every export has a = 0 or the dispatched samples, and the pixels with a = 0 are 0 and the same in a scene',
          exports > 0 and not wrong, f'{exports} exports; ' + ('; '.join(wrong) if wrong else 'all as expected'))


def record(manifest, rows, ground_truth_samples, biases):
    today = date.today().isoformat()
    for (scene, shading), row in rows.items():
        entry = manifest['runs'][slope_name(scene, shading, 0)]
        errors = row['errors']
        last = max(errors)
        at_ground_truth = errors.get(ground_truth_samples)
        seconds = entry['exports'].get(str(ground_truth_samples), {}).get('seconds')
        append_record_row(CONVERGENCE_RECORD_SECTION, [
            today, entry['code'], *entry['machine'], scene, shading, f'{row["gpu_ms"]:.3f}', f'{errors[1024]:.4g}', str(last),
            f'{errors[last]:.4g}', f'{row["slope"]:.3f}', f'{row["last_slope"]:.3f}', f'{row["share"]:.3f}', str(ground_truth_samples),
            f'{at_ground_truth:.4g}' if at_ground_truth is not None else '-', f'{seconds:.0f}' if seconds is not None else '-'])
    for criterion, scene, comparison, samples, result in biases:
        entry = manifest['runs'][slope_name(scene, 'Lambert', 0)]
        append_record_row(BIAS_RECORD_SECTION, [
            today, entry['code'], *entry['machine'], f'[{criterion}]', scene, comparison, str(samples), f'{result.difference:.2e}',
            f'{result.difference_bound:.2e}', f'{result.rms:.2e}', f'{result.rms_bound:.2e}'])


def command_check(args):
    check = Checks()
    manifest, stale = usable_runs(read_manifest())
    check('every run kept for the measurement scenes was accumulated with the scene file, camera, light and Sky as they are now', not stale,
          f'{len(manifest["runs"])} runs as now' + (f'; not used: {", ".join(stale)}; accumulate them again' if stale else ''))
    try:
        rows, scene, samples = check_convergence(manifest, check)
        biases = check_bias(manifest, check, scene, samples)
        check_counts(manifest, check)
    except (KeyError, ImageFileError) as error:
        check('every run of accumulate is kept', False, f'{error!r}; run accumulate for the missing group')
        rows, samples, biases = {}, None, []
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    if args.record and not check.failures:
        record(manifest, rows, samples, biases)
        print(f'recorded in docs/reference-path-tracer-measurements.md')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('accumulate', help='accumulate the runs in Profile and keep the exports')
    command.add_argument('--group', action='append', choices=list(GROUPS), help='accumulate only this group; may be repeated')
    command.add_argument('--no-build', action='store_true', help='use the exe as it is')
    command.set_defaults(handler=command_accumulate)
    command = commands.add_parser('check', help='judge the criteria from the kept exports')
    command.add_argument('--record', action='store_true', help='append the measurements to the record when everything passes')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (HarnessError, ImageFileError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
