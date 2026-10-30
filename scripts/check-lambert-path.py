"""PathTracer 의 Lambert path loop 가 Max Bounces 를 올릴 때 앞의 path 를 이어 가는지, 설정이 누적을 다시 시작하는지 확인하고 1 spp 의
비용을 잰다.

  python scripts/check-lambert-path.py check   # Debug 로 띄워 [6], [8] ~ [10] 과 D3D12 message 를 판정한다
  python scripts/check-lambert-path.py cost    # Profile 로 띄워 1 spp 의 GPU time 을 잰다. --record 를 주면 RECORD 에 덧붙인다

둘 다 app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1 이다. furnace 의 답, SCENE 의 값의
범위, Russian roulette 의 bias 는 같은 run 을 쌓는 check-path-tracer-export.py 가 export 로 판정한다.

판정은 harness baseline 의 시점에서 UI 를 숨기고 찍은 scene capture 로 한다. scene capture 는 post effects 전의 scene color 이고 그
format 이 R11G11B10_FLOAT 라서, PT 의 평균은 이 format 으로 양자화되어 있다. 이 format 은 1.0 을 정확히 담으므로 white furnace 의 답을
bit 로 비교한다.

PT 의 Shading 은 pathtracerchecks.BASE 의 Lambert 다. Lambert 의 f cos / pdf 는 albedo 이고 PT 는 그 값을 곱하므로
(PathTracerCS.hlsl), albedo 가 1 이면 throughput 은 fp32 에서 정확하고 white furnace 의 답은 bit 단위로 맞아야 한다. white furnace 는
Geometric Normals 를 켜서 normal map 이 잃는 energy 를 빼고, 분산이 0 이라 FURNACE_SAMPLES 로 본다. sky 는 늘 흰 Sky Light 의 Intensity 1 이다.
SCENE 은 furnace 와 directional light 를 끈 Sponza 다.

  [6] Russian roulette 를 끄고 Max Bounces 를 BOUNCE_STEPS 로 올리면 같은 Seed 와 FURNACE_SAMPLES 에서 어떤 pixel 도 어두워지지
      않고, image 평균은 VISIBLE_BOUNCES 까지 매번 는다. RNG 를 쓰는 양이 Max Bounces 에 따라 달라지지 않아 path 가 앞 run 의 path 를
      그대로 이어 가므로 pixel 이 어두워질 수 없다. VISIBLE_BOUNCES 뒤의 반사가 더하는 빛은 pixel 값의 양자화보다 작다.

설정과 reset:

  [8] PT 의 설정이 state 에 보이고 settings set 으로 바뀌며, 끝낼 때 editor.ini 에 남는다.
  [9] image 를 정하는 설정을 하나씩 바꾸면 pt 의 resets 가 1 늘고 samples 를 처음부터 센다. Samples per Frame 과 Target Samples 를
      바꾸면 늘지 않는다.
  [10] SCENE 을 쌓다가 white furnace 로 바꾼 뒤 처음 찍은 capture 에서 모든 pixel 이 1.0 이다. reset 뒤에 옛 sample 이 남지 않는다.

check 는 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다. [6] 의 Max Bounces 1
과 64 의 capture 는 발표에 쓰도록 KEPT_DIR 에 DDS 와 보기용 PNG 로 남긴다.

cost 는 SCENE 을 Max Bounces 64, Samples per Frame 1 로 쌓는 동안 Profiler 의 "Path Tracer" GPU time 을 COST_FRAMES 동안 읽는다.
Russian roulette 를 켠 것과 끈 것을 잰다.
"""

import argparse
import shutil
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import (BASE, FURNACE_SAMPLES, RECORD, light_by_sky_only, SECTION, WHITE_FURNACE, append_record_row, code_version,
                              describe_machine, path_tracer_state, set_path_tracer, SKY_RADIANCE)
from viewer import HARNESS_DIR, PREVIEW_EXPOSURE, HarnessError, Session, capture, make_view, start_instance, stop_instance

KEPT_DIR = HARNESS_DIR / 'kept' / 'lambert-path'
COST_RECORD_SECTION = '## 작업 5: 1 spp 의 GPU time'

BOUNCE_STEPS = [1, 2, 4, 16, 64]
# Max Bounces 16 과 64 의 capture 는 bit 까지 같았다. 16 번 뒤의 반사가 더하는 빛이 SCENE 의 pixel 값 (평균 0.004) 에서
# R11G11B10 의 반 ulp (0.004 근처에서 약 3e-5) 보다 작은 것으로 본다 (추측).
VISIBLE_BOUNCES = 16
# [9] 에서 설정을 바꾸기 전에 쌓아 두는 sample 수. 바꾼 뒤 몇 frame 안에 이만큼 다시 쌓이지 않을 만큼 둔다.
SAMPLES_BEFORE_CHANGE = 32
COST_WARMUP_SAMPLES = 16
COST_FRAMES = 256
# 가장 오래 기다리는 명령은 cost 가 Russian roulette 를 끄고 COST_FRAMES 동안 재는 profile 로, 1 분쯤 걸린다.
WAIT_TIMEOUT = 900

SCENE = BASE

# [9] 에서 바꿔 보는 값. 모두 BASE 와 다르다.
IMAGE_SETTING_CHANGES = {'Output': 'Indirect Irradiance', 'Shading': 'glTF', 'Seed': 1, 'Max Bounces': 63, 'Russian Roulette': 0, SKY_RADIANCE: 2, 'Geometric Normals': 1,
                         'Furnace': 1, 'Furnace Albedo': 0.5, 'Furnace Emission': 0.5}
PACE_SETTING_CHANGES = {'Samples per Frame': 2, 'Target Samples': 8192}
# [8] 에서 끝내기 전에 두는 값. BASE 와도 IMAGE_SETTING_CHANGES 와도 달라서 editor.ini 에서 이 run 이 쓴 값인지 알 수 있다. Output 과
# Shading 은 IMAGE_SETTING_CHANGES 와 같은 값이지만 [9] 가 BASE 로 되돌린 뒤에 바꾸므로 같은 것을 알 수 있다. Output 은 값이 둘뿐이라
# BASE 와도 IMAGE_SETTING_CHANGES 와도 다른 값이 없다.
SAVED_SETTINGS = {'Output': 'Indirect Irradiance', 'Shading': 'glTF', 'Max Bounces': 7, 'Russian Roulette': 0, 'Geometric Normals': 1, 'Furnace': 1,
                  'Furnace Albedo': 0.75, 'Furnace Emission': 0.125, 'Seed': 3}


def accumulate(session, name, settings, samples):
    """settings 로 처음부터 samples 만큼 쌓은 scene capture 의 rgb 와 그 DDS 의 경로."""
    set_path_tracer(session, settings | {'Target Samples': samples})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={samples}', timeout=WAIT_TIMEOUT)
    path = capture(session, 'scene', name)
    return read_dds_rgba32f(path)[..., :3], path


def keep(path):
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    kept = KEPT_DIR / path.name
    shutil.copy2(path, kept)
    make_view(kept, 'scene', kept.with_suffix('.png'), None, 1 << 16, PREVIEW_EXPOSURE)


def count_not(rgb, value):
    return int(np.count_nonzero(np.any(rgb != value, axis=2)))


def describe_values(rgb):
    return f'min {rgb.min():.9g}, max {rgb.max():.9g}, mean {rgb.mean():.9g}'


def check_bounces(session, check):
    images, bounce_means = [], []
    for bounces in BOUNCE_STEPS:
        rgb, path = accumulate(session, f'scene-b{bounces}-rr0', SCENE | {'Max Bounces': bounces, 'Russian Roulette': 0}, FURNACE_SAMPLES)
        images.append(rgb)
        bounce_means.append(float(rgb.mean(dtype=np.float64)))
        if bounces in (BOUNCE_STEPS[0], BOUNCE_STEPS[-1]):
            keep(path)
    steps = list(zip(BOUNCE_STEPS, BOUNCE_STEPS[1:]))
    darker = [int(np.count_nonzero(np.any(later < earlier, axis=2))) for earlier, later in zip(images, images[1:])]
    changed = [int(np.count_nonzero(np.any(later != earlier, axis=2))) for earlier, later in zip(images, images[1:])]
    check(f'[6] no pixel of the scene gets darker as Max Bounces goes {BOUNCE_STEPS}', not any(darker),
          ', '.join(f'{fewer} -> {more}: {count} darker, {pixels} changed' for (fewer, more), count, pixels in zip(steps, darker, changed)))
    growing = all(later > earlier for (_, more), earlier, later in zip(steps, bounce_means, bounce_means[1:]) if more <= VISIBLE_BOUNCES)
    check(f'[6] scene mean grows with each Max Bounces step up to {VISIBLE_BOUNCES}', growing,
          ', '.join(f'{bounces}: {mean:.9g}' for bounces, mean in zip(BOUNCE_STEPS, bounce_means)))


def check_resets(session, check):
    set_path_tracer(session, SCENE | {'Target Samples': 4096})
    session.ask_ok('pt reset')
    for key, value in list(IMAGE_SETTING_CHANGES.items()) + list(PACE_SETTING_CHANGES.items()):
        session.ask_ok(f'pt wait samples={SAMPLES_BEFORE_CHANGE}', timeout=WAIT_TIMEOUT)
        before = path_tracer_state(session)
        set_path_tracer(session, {key: value})
        session.ask_ok('frames 2')
        after = path_tracer_state(session)
        restarted = after['resets'] == before['resets'] + 1 and after['samples'] < before['samples']
        kept_on = after['resets'] == before['resets'] and after['samples'] >= before['samples']
        detail = f'resets {before["resets"]} -> {after["resets"]}, samples {before["samples"]} -> {after["samples"]}'
        if key in IMAGE_SETTING_CHANGES:
            check(f'[9] changing {key} starts over', restarted, detail)
        else:
            check(f'[9] changing {key} keeps accumulating', kept_on, detail)
        set_path_tracer(session, {key: (SCENE | {'Target Samples': 4096})[key]})

    accumulate(session, 'scene-before-furnace', SCENE, FURNACE_SAMPLES)
    set_path_tracer(session, WHITE_FURNACE | {'Target Samples': FURNACE_SAMPLES})
    rgb = read_dds_rgba32f(capture(session, 'scene', 'white-furnace-after-scene'))[..., :3]
    samples = path_tracer_state(session)['samples']
    check('[10] the first capture after switching from the scene to the white furnace is 1.0 everywhere', count_not(rgb, 1.0) == 0,
          f'{count_not(rgb, 1.0)} pixels are not, {samples} samples by now; {describe_values(rgb)}')


def check_settings_state(session, check):
    replied = set_path_tracer(session, SAVED_SETTINGS)['state']['settings'][SECTION]
    read = session.ask_ok(f'state settings.{SECTION}')['state']['settings'][SECTION]
    expected = {key: str(value) for key, value in SAVED_SETTINGS.items()}
    shown = {key: read.get(key) for key in expected}
    check('[8] settings set changes the path tracer settings in state', shown == expected and replied == read, shown)


def read_ini_section(path, section):
    values, current = {}, None
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1]
        elif current == section and '=' in line and not line.startswith(';'):
            key, value = line.split('=', 1)
            values[key.strip()] = value.strip()
    return values


def check_stopped(instance, check):
    stop_and_check(instance, check)
    saved = read_ini_section(Path(instance['run_dir']) / 'editor.ini', SECTION)
    expected = {key: str(value) for key, value in SAVED_SETTINGS.items()}
    check('[8] editor.ini keeps the path tracer settings', {key: saved.get(key) for key in expected} == expected, saved)


def command_check(args):
    check = Checks()
    instance = start_instance('Debug', build_first=not args.no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            light_by_sky_only(session)
            check_bounces(session, check)
            check_resets(session, check)
            check_settings_state(session, check)
        finally:
            session.close()
        check_stopped(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('check ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
    print(f'kept captures: {KEPT_DIR}')
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def command_cost(args):
    code = code_version()
    instance = start_instance('Profile', build_first=not args.no_build)
    rows = []
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            light_by_sky_only(session)
            config, gpu, scene = describe_machine(session)
            print(f'{config}, {gpu}, scene {scene}, Max Bounces {SCENE["Max Bounces"]}, Samples per Frame {SCENE["Samples per Frame"]}')
            for russian_roulette in (1, 0):
                set_path_tracer(session, SCENE | {'Russian Roulette': russian_roulette, 'Target Samples': 1 << 20})
                session.ask_ok('pt reset')
                session.ask_ok(f'pt wait samples={COST_WARMUP_SAMPLES}', timeout=WAIT_TIMEOUT)
                measured = session.ask_ok(f'profile Path Tracer frames={COST_FRAMES}', timeout=WAIT_TIMEOUT)['gpu_ms']
                state = 'on' if russian_roulette else 'off'
                print(f'Russian roulette {state}: GPU time over {COST_FRAMES} frames '
                      f'mean {measured["mean"]} ms, min {measured["min"]} ms, max {measured["max"]} ms')
                rows.append([time.strftime('%Y-%m-%d'), code, config, gpu, scene, str(SCENE['Max Bounces']), state,
                             str(COST_FRAMES)] + [f'{float(measured[key]):.3f}' for key in ('mean', 'min', 'max')])
        finally:
            session.close()
    finally:
        stopped = stop_instance(instance)
    print(f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    if stopped['exit_reason'] != 'normal':
        return 1
    if args.record:
        for row in rows:
            append_record_row(COST_RECORD_SECTION, row)
        print(f'recorded in {RECORD}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    for name, handler, help in [('check', command_check, 'judge [6], [8] to [10] and the D3D12 messages in Debug'),
                                ('cost', command_cost, 'measure the GPU time of 1 spp in Profile')]:
        command = commands.add_parser(name, help=help)
        command.add_argument('--no-build', action='store_true', help='use the exe as it is')
        command.set_defaults(handler=handler)
        if name == 'cost':
            command.add_argument('--record', action='store_true', help=f'append the measured times to {RECORD.name}')
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (HarnessError, ImageFileError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
