"""Sky Light 한 곳이 sky 의 radiance 를 정하고, raster 의 배경과 path tracer 의 miss 가 그 값을 쓰는지 확인한다.

  python scripts/check-sky-light.py check   # Debug 로 띄워 [1] ~ [5] 와 D3D12 message 를 판정한다
  python scripts/check-sky-light.py keep    # Profile 로 띄워 같은 시점의 raster 와 PT 의 display 를 KEPT_DIR 에 남긴다

번호는 작업 9 의 완료 기준의 번호다. [6] (주석) 은 code 를 읽어 본다.

app 을 직접 띄우고 끝낸다. 하나라도 실패하면 exit 1 이다. 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때
LIVE_DEVICE 하나만 남는지도 본다.

판정은 harness baseline 에서 UI 를 숨기고 SKY_POSE 에서 찍은 capture 로 한다. sky pixel 은 Engine Tuning 의 Ray Tracing/View 가 Debug: Front Face 인 capture 에서
아무 면도 만나지 않은 pixel 의 가장자리 EDGE_PIXELS 를 깎은 것이다. sky pixel 이 화면의 MIN_SKY_FRACTION 보다 적으면 [2], [3] 을
판정하지 않고 실패한다. scene capture 는 post effects 전의 scene color 이고 format 이 R11G11B10_FLOAT 다.

  [1] Sky Light 밖에 sky 를 정하는 설정이 없다. settings set Engine Tuning: Path Tracer/Sky Radiance=1 이 error 이고, Engine Tuning 의 Path Tracer 에 Sky 가
      든 key 가 없다.
  [2] SKY_LIGHTS 의 값마다 PT 를 끈 scene capture 의 sky pixel 이 모두 color x intensity 다. 값은 R11G11B10 이 정확히 담는 것으로
      골라 bit 로 비교한다.
  [3] 같은 값에서 PT 를 켜고 Shading glTF 와 Lambert 로 PT_SAMPLES 만큼 쌓으면, export 의 sky pixel 이 모두 color x intensity (fp32)
      와 bit 가 같고, scene capture 가 [2] 의 PT 를 끈 capture 와 sky pixel 에서 bit 가 같다. ROUNDED_SKY_LIGHT 처럼 R11G11B10 이
      정확히 담지 못하는 값에서도 PT 의 UAV 쓰기와 raster 의 render target 쓰기가 같은 값으로 반올림하는지 본다.
  [4] PT 를 쌓는 동안 Color 나 Intensity 를 바꾸면 pt 의 resets 가 1 늘고 samples 를 처음부터 센다. Intensity 의 inf 는 받고
      (check-path-tracer-export.py 가 쓴다) NaN 과 음수는 받지 않아 editor set 이 error 이고 값이 그대로다. scene file 에 쓰고 다시 읽는 것은 check-scene-open.py 의
      [O11] 이 본다.
  [5] state sky 의 color 와 intensity 가 editor get 의 Sky Light 와 같은 값이고, Scene Hierarchy panel 의 목록 (state editor 의
      objects) 에 Sky Light 가 있어 Details panel 에서 그 값을 바꿀 수 있다.

keep 은 기본 시점 (harness baseline 의 pose) 과 SKY_POSE 에서 PT 를 끈 display 와 PT 를 Shading glTF 로 KEEP_SAMPLES 만큼 쌓은
display 를 찍는다. harness baseline 이라 Adaptation 이 꺼져 있고 Exposure 가 같다.
"""

import argparse
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from imagefiles import ImageFileError, read_dds_rgba32f
from imagemasks import eroded
from pathtracerchecks import (BASE, KEY_PREFIX, SECTION, SKY_COLOR, SKY_LIGHT_OBJECT, SKY_RADIANCE, path_tracer_settings, path_tracer_state,
                              set_path_tracer)
from viewer import HARNESS_DIR, SETTLE_FRAMES, HarnessError, Session, baseline_scene_pose, capture, start_instance, stop_instance

KEPT_DIR = HARNESS_DIR / 'kept' / 'sky-light'

# check-raster-lighting.py 가 sky 를 보던 시점이다. courtyard 가운데에서 위를 본다.
SKY_POSE = '0 300 0 1.5708 1.2'
EDGE_PIXELS = 2
MIN_SKY_FRACTION = 0.1

# (color, intensity). 성분과 곱이 모두 R11G11B10 이 정확히 담는 값이다.
SKY_LIGHTS = [((1.0, 1.0, 1.0), 1.0), ((1.0, 1.0, 1.0), 0.25), ((1.0, 1.0, 1.0), 2.0), ((0.5, 0.75, 1.0), 1.0)]
# 0.6 은 R11G11B10 의 어느 channel 도 정확히 담지 못한다.
ROUNDED_SKY_LIGHT = ((1.0, 1.0, 1.0), 0.6)
# [5] 에서 두는 값. 기본값과도 SKY_LIGHTS 의 어느 것과도 달라서 state 가 이 값을 따랐는지 알 수 있다.
STATE_SKY_LIGHT = ((0.25, 0.5, 1.0), 0.75)

# miss 한 sample 은 모두 같은 값이라 sample 수와 상관없이 평균이 그 값이다. 수를 줄여 시간을 아낀다.
PT_SAMPLES = 4
PATH_TRACER = {key: value for key, value in BASE.items() if key not in (SKY_COLOR, SKY_RADIANCE)} | {'Target Samples': PT_SAMPLES}
SHADINGS = ['glTF', 'Lambert']
# [4] 에서 바꾸기 전에 쌓아 두는 sample 수. 바꾼 뒤 frames 2 동안 쌓이는 수보다 커야 처음부터 센 것을 알 수 있다.
SAMPLES_BEFORE_CHANGE = 32
WAIT_TIMEOUT = 900

KEEP_SAMPLES = 4096
KEEP_TIMEOUT = 3600


def format_color(color):
    return ','.join(f'{value:.9g}' for value in color)


def set_sky_light(session, color, intensity):
    return session.ask_ok(f'editor set {SKY_LIGHT_OBJECT}: Color={format_color(color)}; Intensity={intensity:.9g}')


def radiance(color, intensity):
    """SkyLight::GetRadiance 처럼 fp32 로 곱한 값"""
    return np.asarray(color, dtype=np.float32) * np.float32(intensity)


def describe(sky_light):
    color, intensity = sky_light
    return f'color ({format_color(color)}) x {intensity:g}'


def sky_mask(session):
    session.ask_ok('settings set Engine Tuning: Ray Tracing/View=Debug: Front Face')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    front_face = np.asarray(Image.open(capture(session, 'overlay', 'sky-pose-front-face')).convert('RGB'))
    session.ask_ok('settings set Engine Tuning: Ray Tracing/View=Off')
    return eroded(np.all(front_face == 0, axis=2), EDGE_PIXELS)


def count_not(pixels, value):
    """pixels 중 value 와 bit 가 다른 것의 수"""
    return int(np.count_nonzero(np.any(pixels.view(np.uint32) != np.asarray(value, dtype=np.float32).view(np.uint32), axis=-1)))


def check_settings_location(session, check):
    tried = session.ask(f'settings set {SECTION}: {KEY_PREFIX}Sky Radiance=1')
    keys = path_tracer_settings(session.ask_ok(f'state settings.{SECTION}')['state']['settings'][SECTION])
    sky_keys = [key for key in keys if 'Sky' in key]
    check('[1] the path tracer has no sky setting of its own', not tried['ok'] and not sky_keys,
          f'settings set {SECTION}: {KEY_PREFIX}Sky Radiance=1 replied {"ok" if tried["ok"] else "an error"}; keys with Sky: {sky_keys}')


def raster_scene(session, name):
    set_path_tracer(session, {'Show': 0})
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    return read_dds_rgba32f(capture(session, 'scene', name))[..., :3]


def accumulate(session, shading, name):
    """shading 으로 처음부터 PT_SAMPLES 만큼 쌓은 export 의 rgb 와 scene capture 의 rgb"""
    set_path_tracer(session, PATH_TRACER | {'Shading': shading})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={PT_SAMPLES}', timeout=WAIT_TIMEOUT)
    export = read_dds_rgba32f(capture(session, 'pt', f'{name}-pt'))[..., :3]
    scene = read_dds_rgba32f(capture(session, 'scene', f'{name}-scene'))[..., :3]
    return export, scene


def check_sky_light(session, sky, sky_light, index, exact, check):
    color, intensity = sky_light
    set_sky_light(session, color, intensity)
    expected = radiance(color, intensity)
    name = f'sky-{index}'
    raster = raster_scene(session, f'{name}-raster')
    if exact:
        check(f'[2] the raster draws the sky pixels as {describe(sky_light)}', count_not(raster[sky], expected) == 0,
              f'{count_not(raster[sky], expected)} of {int(sky.sum())} sky pixels are not {expected.tolist()}')
    for shading in SHADINGS:
        export, scene = accumulate(session, shading, f'{name}-{shading.lower()}')
        check(f'[3] {shading} path tracer exports the sky pixels as {describe(sky_light)}', count_not(export[sky], expected) == 0,
              f'{count_not(export[sky], expected)} of {int(sky.sum())} sky pixels are not {expected.tolist()}')
        differing = int(np.count_nonzero(np.any(scene[sky].view(np.uint32) != raster[sky].view(np.uint32), axis=-1)))
        check(f'[3] {shading} path tracer draws the sky pixels of the scene as the raster does with {describe(sky_light)}', differing == 0,
              f'{differing} of {int(sky.sum())} sky pixels differ; path tracer {np.unique(scene[sky], axis=0)[:3].tolist()}, '
              f'raster {np.unique(raster[sky], axis=0)[:3].tolist()}')


def check_resets(session, check):
    color, intensity = SKY_LIGHTS[0]
    set_sky_light(session, color, intensity)
    set_path_tracer(session, PATH_TRACER | {'Target Samples': 4096})
    session.ask_ok('pt reset')
    for key, value in [('Intensity', '0.5'), ('Color', '1,0.5,1')]:
        session.ask_ok(f'pt wait samples={SAMPLES_BEFORE_CHANGE}', timeout=WAIT_TIMEOUT)
        before = path_tracer_state(session)
        session.ask_ok(f'editor set {SKY_LIGHT_OBJECT}: {key}={value}')
        session.ask_ok('frames 2')
        after = path_tracer_state(session)
        check(f'[4] changing the Sky Light {key} starts the path tracer over',
              after['resets'] == before['resets'] + 1 and after['samples'] < before['samples'],
              f'resets {before["resets"]} -> {after["resets"]}, samples {before["samples"]} -> {after["samples"]}')
    set_sky_light(session, color, intensity)


def read_intensity(session):
    return session.ask_ok(f'editor get {SKY_LIGHT_OBJECT}')['values']['Intensity']


def check_values_taken(session, check):
    color, intensity = SKY_LIGHTS[0]
    for value in ['nan', '-1']:
        set_sky_light(session, color, intensity)
        reply = session.ask(f'editor set {SKY_LIGHT_OBJECT}: Intensity={value}')
        kept = read_intensity(session)
        check(f'[4] the Sky Light does not take the intensity {value}', not reply['ok'] and kept == f'{intensity:.9g}',
              f'editor set replied {"ok" if reply["ok"] else "an error"}; Intensity reads {kept}')
    session.ask(f'editor set {SKY_LIGHT_OBJECT}: Intensity=inf')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    taken = read_intensity(session)
    check('[4] the Sky Light takes an infinite intensity', taken == 'inf', f'Intensity reads {taken}')
    set_sky_light(session, color, intensity)


def check_state(session, check):
    color, intensity = STATE_SKY_LIGHT
    set_sky_light(session, color, intensity)
    state = session.ask_ok('state sky')['state']['sky']
    settings = session.ask_ok(f'editor get {SKY_LIGHT_OBJECT}')['values']
    shown = {'Color': format_color([float(state['color'][channel]) for channel in 'rgb']), 'Intensity': f'{float(state["intensity"]):.9g}'}
    expected = {'Color': format_color(color), 'Intensity': f'{intensity:.9g}'}
    check('[5] state sky has the values of the Sky Light settings', shown == settings == expected,
          f'state sky {shown}, settings {settings}')
    objects = session.ask_ok('state editor')['state']['editor']['objects']
    check('[5] the Scene Hierarchy lists the Sky Light', SKY_LIGHT_OBJECT in objects, f'objects {list(objects)}')


def command_check(args):
    check = Checks()
    instance = start_instance('Debug', build_first=not args.no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_settings_location(session, check)
            session.ask_ok(f'pose {SKY_POSE}')
            sky = sky_mask(session)
            fraction = float(sky.mean())
            if check(f'the sky pose shows at least {MIN_SKY_FRACTION:.0%} sky pixels', fraction >= MIN_SKY_FRACTION,
                     f'{int(sky.sum())} sky pixels ({fraction:.1%} of the scene)'):
                for index, sky_light in enumerate(SKY_LIGHTS):
                    check_sky_light(session, sky, sky_light, index, True, check)
                check_sky_light(session, sky, ROUNDED_SKY_LIGHT, len(SKY_LIGHTS), False, check)
            check_resets(session, check)
            check_values_taken(session, check)
            check_state(session, check)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('the app ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def keep_display(session, name):
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    path = capture(session, 'display', name)
    kept = KEPT_DIR / f'{name}{path.suffix}'
    shutil.copy2(path, kept)
    return kept


def command_keep(args):
    instance = start_instance('Profile', build_first=not args.no_build)
    kept = []
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            for pose_name, pose in [('start', baseline_scene_pose()), ('sky-pose', SKY_POSE)]:
                session.ask_ok(f'pose {pose}')
                set_path_tracer(session, {'Show': 0})
                session.ask_ok(f'frames {SETTLE_FRAMES}')
                kept.append(keep_display(session, f'{pose_name}-raster'))
                set_path_tracer(session, PATH_TRACER | {'Shading': 'glTF', 'Samples per Frame': 16, 'Target Samples': KEEP_SAMPLES})
                session.ask_ok('pt reset')
                session.ask_ok(f'pt wait samples={KEEP_SAMPLES}', timeout=KEEP_TIMEOUT)
                kept.append(keep_display(session, f'{pose_name}-path-tracer'))
        finally:
            session.close()
    except HarnessError:
        stop_instance(instance, kill_now=True)
        raise
    print(stop_instance(instance))
    for path in kept:
        print(f'kept {path}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    check_parser = commands.add_parser('check', help='judge [1] ~ [5] with the Debug app')
    check_parser.add_argument('--no-build', action='store_true', help='run the Debug app as it is built')
    check_parser.set_defaults(handler=command_check)
    keep_parser = commands.add_parser('keep', help='keep the raster and path tracer displays of the same poses with the Profile app')
    keep_parser.add_argument('--no-build', action='store_true', help='run the Profile app as it is built')
    keep_parser.set_defaults(handler=command_keep)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
