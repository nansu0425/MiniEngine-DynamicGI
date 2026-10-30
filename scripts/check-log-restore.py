"""측정 조건의 줄 하나로 app 을 다시 띄운 뒤에도 같은 조건과 같은 path tracer image 로 돌아오는지 확인한다.

  python scripts/check-log-restore.py check   # Debug 로 두 번 띄워 아래 기준을 모두 판정한다

번호는 작업 10 의 목표 번호다. G3 의 사람 쪽 (PT panel 의 Log 와 Restore 칸) 은 computer use 로, G5 의 selftest 와 snapshot 은
viewer.py 로 본다.

app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1 이다. 실행마다 끝낼 때 실행 중의
D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다.

첫 실행에서 LINE_A 와 LINE_B 의 조건을 명령으로 두고 줄을 얻는다. A 는 path tracer 를 SAMPLES 만큼 쌓아 capture pt 로 export 한
image 의 줄이다. B 는 Furnace 를 켠 줄이다. Furnace 는 directional light 를 끄므로 light 의 값이 image 에 보이지 않아 A 와 나눈다.
두 번째 실행은 harness baseline 에서 다시 시작하므로 조건이 모두 기본값으로 돌아간 상태다.

  [G1] 다시 띄운 직후, A, B 의 줄이 같은 key 를 가지고, size 와 scene 밖의 key 는 모두 "다시 띄운 직후와 A" 또는 "A 와 B" 사이에서
       값이 다르다. 그래서 아래의 restore 가 key 마다 실제로 값을 바꾼다. size 는 되돌리지 않는 조건이라 [G4] 가 따로 본다. A 와 B 는
       같은 scene 의 줄이라 scene 은 [G4] 가 열 수 없는 scene file 의 줄과 scene 이 없는 줄로 본다. 다른 scene 의 줄이 그 scene 을 여는
       것은 check-scene-open.py 의 [O6] 이 본다.
  [G2a] 다시 띄운 app 에 A 를, 그 뒤 B 를 restore 하면 그때마다 줄 (pt samples 밖) 과 state 의 camera, light, sky,
       settings.Engine Tuning, settings.Path Tracer (Show 밖) 가 줄을 얻을 때와 문자열로 같다. Target Samples 는 줄의 pt samples 다.
  [G2b] 다시 띄운 app 에서 export 의 줄을 restore 하고 Show 만 켜 SAMPLES 까지 쌓아 export 한 image 의 rgba 가 첫 실행의 export 와
       bit 단위로 같다.
  [G3] capture pt 의 응답의 line 이 옆에 남긴 .conditions.txt 와 같고, 쌓기를 멈춘 뒤의 describe 와 같다. Validation view 의
       validation capture 의 줄도 describe 의 조건을 모두 담고, 그 줄을 restore 하면 그 조건으로 돌아온다.
  [G4] A 의 줄에서 key 하나를 빼거나 값 하나를 받을 수 없는 값으로 바꾼 줄을 restore 하면 error 이고, unread 가 그 key 를 담고,
       state 가 바뀌지 않는다. size 만 다른 줄은 restore 되고, note 가 줄과 지금의 해상도를 알리며 scene 의 크기는 그대로다.
       taa=on 인 B 의 restore 는 note 로 jitter 를 재현할 수 없다고 알린다. scene 이 없는 줄은 scene file 전의 줄이라 -scene 없이 연
       scene 의 줄로 restore 되고, 열 수 없는 scene file 의 줄 (위의 scene=x) 은 error 다.

A 의 두 export 와 보기용 PNG, 줄은 발표에 쓰도록 KEPT_DIR 에 남긴다.
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import SKY_LIGHT_SECTION
from viewer import (HARNESS_DIR, PREVIEW_EXPOSURE, VALIDATION_VIEW_SETTLE_FRAMES, HarnessError, Session, make_view, shots_dir, start_instance,
                    stop_instance, without_time_varying)

KEPT_DIR = HARNESS_DIR / 'kept' / 'log-restore'

SAMPLES = 16
WAIT_TIMEOUT = 900

# A 는 작업 8 이 흰 점을 잰 복도의 시점이다. Max Bounces 를 줄이고 Russian roulette 를 꺼, 끈 경우의 path 를 짧게 둔다. Output 은
# Indirect Irradiance 로 두어 [G2b] 가 그 image 의 재현도 본다.
LINE_A = {
    'pose': '-1479.30859 211.686859 395.723236 -1.53974497 -0.124806166',
    'light': 'elevation=40 azimuth=200 color=0.5,0.75,1 intensity=3',
    'sky': 'Color=0.75,0.5,1; Intensity=0.5',
    'taa': 'off',
    'path tracer': {'Output': 'Indirect Irradiance', 'Shading': 'glTF', 'Seed': 7, 'Max Bounces': 8, 'Russian Roulette': 0,
                    'Geometric Normals': 1, 'Furnace': 0, 'Furnace Albedo': 0.75, 'Furnace Emission': 0.25},
    'samples': SAMPLES,
}
# A 와 모든 조건이 다르다.
LINE_B = {
    'pose': '0 300 0 1.5708 1.2',
    'light': 'elevation=20 azimuth=10 color=1,0.5,0.25 intensity=5',
    'sky': 'Color=1,1,0.5; Intensity=2',
    'taa': 'on',
    'path tracer': {'Output': 'Radiance', 'Shading': 'Lambert', 'Seed': 2, 'Max Bounces': 16, 'Russian Roulette': 1,
                    'Geometric Normals': 0, 'Furnace': 1, 'Furnace Albedo': 0.5, 'Furnace Emission': 0.5},
    'samples': 4,
}
TAA = 'Graphics/AA/TAA/Enable'
CONDITION_STATE = ['camera', 'light', 'sky', 'settings.Engine Tuning', 'settings.Path Tracer']

# [G4] 에서 key 마다 넣는 받을 수 없는 값. 모든 key 에 'x' 를 넣는 것과 함께, 형식은 맞지만 범위나 조합이 틀린 값을 넣는다.
# light direction 의 값은 그 줄의 light up 과 직교하지 않는다.
OUT_OF_RANGE = [('camera', '1,2'), ('heading', '1.5abc'), ('pitch', '0.1,0'), ('light direction', '0,0,1'), ('light color', '1.5,0,0'),
                ('light intensity', '-1'), ('light intensity', 'inf'), ('sky color', '1,1'), ('sky intensity', 'nan'), ('sky intensity', '-1'),
                ('pt output', 'radiance'), ('pt shading', 'gltf'), ('pt seed', '-1'), ('pt max bounces', '65537'), ('pt russian roulette', '2'),
                ('pt furnace', 'on'), ('pt geometric normals', ''), ('pt furnace albedo', '1.5'), ('pt furnace albedo', 'nan'), ('pt furnace emission', 'inf'), ('pt samples', '1048577'),
                ('size', '1920x'), ('taa', 'maybe')]


def parse(line):
    return dict(item.split('=', 1) for item in line.split('; '))


def format_line(values):
    return '; '.join(f'{key}={value}' for key, value in values.items())


def without(values, *keys):
    return {key: value for key, value in values.items() if key not in keys}


def read_conditions_state(session):
    """CONDITION_STATE 의 state 를 path 마다. Show 는 줄의 조건이 아니라서 뺀다."""
    state = {}
    for path in CONDITION_STATE:
        part = session.ask_ok(f'state {path}')['state']
        for key in path.split('.'):
            part = part[key]
        state[path] = part
    state['settings.Path Tracer'] = without(state['settings.Path Tracer'], 'Show')
    return state


def set_conditions(session, line):
    session.ask_ok(f'pose {line["pose"]}')
    session.ask_ok(f'light set {line["light"]}')
    session.ask_ok(f'settings set {SKY_LIGHT_SECTION}: {line["sky"]}')
    session.ask_ok(f'settings set Engine Tuning: {TAA}={line["taa"]}')
    settings = line['path tracer'] | {'Show': 1, 'Samples per Frame': 1, 'Target Samples': line['samples']}
    session.ask_ok('settings set Path Tracer: ' + '; '.join(f'{key}={value}' for key, value in settings.items()))
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={line["samples"]}', timeout=WAIT_TIMEOUT)


def export(session, name):
    reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / name}')
    return reply, read_dds_rgba32f(reply['path'])


def keep(path):
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    kept = KEPT_DIR / Path(path).name
    shutil.copy2(path, kept)
    make_view(kept, 'pt', kept.with_suffix('.png'), None, 1 << 16, PREVIEW_EXPOSURE)
    return kept


def check_export_line(session, check, reply, label):
    saved = Path(reply['conditions']).read_text(encoding='utf-8')
    described = session.ask_ok('describe')['line']
    check(f'[G3] the line of {label} is what it left beside the image and what describe gives after the path tracer stopped',
          saved == reply['line'] + '\n' and reply['line'] == described, reply['line'] if reply['line'] == described else f'{reply["line"]} vs {described}')


def first_run(check, no_build):
    """A 와 B 의 조건을 두고 (A 의 줄, A 의 export, A 의 state, B 의 줄, B 의 state) 를 돌려준다."""
    instance = start_instance('Debug', build_first=not no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            set_conditions(session, LINE_A)
            reply, image = export(session, 'line-a')
            check_export_line(session, check, reply, 'capture pt')
            first = (reply['line'], image, read_conditions_state(session))
            kept = keep(reply['path'])
            (KEPT_DIR / 'line-a.conditions.txt').write_text(reply['line'] + '\n', encoding='utf-8')
            print(f'kept {kept}')

            set_conditions(session, LINE_B)
            second = (session.ask_ok('describe')['line'], read_conditions_state(session))
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise
    return first + second


def check_restored(session, check, line, state, label):
    restored = session.ask_ok(f'restore {line}')
    now = session.ask_ok('describe')['line']
    target = session.ask_ok('state settings.Path Tracer')['state']['settings']['Path Tracer']['Target Samples']
    differing = {key: f'{value} vs {parse(now).get(key)}' for key, value in without(parse(line), 'pt samples').items() if parse(now).get(key) != value}
    check(f'[G2a] restore of {label} gives its conditions again, and pt samples as the Target Samples',
          not differing and parse(now).keys() == parse(line).keys() and target == parse(line)['pt samples'],
          f'{format_line(differing) or "same"}; Target Samples {target}, pt samples {parse(line)["pt samples"]}')
    read = read_conditions_state(session)
    differing = [group for group in read if read[group] != state[group]]
    check(f'[G2a] state {", ".join(CONDITION_STATE)} after restore of {label} is as when the line was taken', not differing,
          ', '.join(differing) or 'same')
    return restored


def check_coverage(check, start, line_a, line_b):
    keys = [set(parse(line)) for line in (start, line_a, line_b)]
    check('[G1] the lines at the start, of A and of B have the same keys', keys[0] == keys[1] == keys[2],
          ', '.join(sorted(keys[0] ^ keys[1] | keys[1] ^ keys[2])) or ', '.join(sorted(keys[0])))
    start, line_a, line_b = parse(start), parse(line_a), parse(line_b)
    unchanged = [key for key in line_a if key not in ('size', 'scene') and start.get(key) == line_a[key] and line_a[key] == line_b.get(key)]
    check('[G1] restoring A after the start and then B changes every condition but size and scene at least once', not unchanged,
          ', '.join(unchanged) or f'{len(line_a) - 1} conditions')


def check_unreadable(session, check, line_a):
    values = parse(line_a)
    # scene 이 없는 줄은 받는 줄이라 check_line_without_scene 이 본다.
    lines = [(f'without {key}', key, format_line(without(values, key))) for key in values if key != 'scene']
    lines += [(f'{key}=x', key, format_line(values | {key: 'x'})) for key in values]
    lines += [(f'{key}={value!r}', key, format_line(values | {key: value})) for key, value in OUT_OF_RANGE]
    wrong = []
    for label, key, line in lines:
        before = without_time_varying(session.ask_ok('state')['state'])
        response = session.ask(f'restore {line}')
        after = without_time_varying(session.ask_ok('state')['state'])
        if response['ok'] or key not in response.get('unread', '').split(', ') or after != before:
            wrong.append(f'{label} ({"ok" if response["ok"] else response.get("unread")}{", state changed" if after != before else ""})')
    check(f'[G4] restore of a line that lacks a key or cannot set one fails, names the key in unread and changes nothing', not wrong,
          '; '.join(wrong) or f'{len(lines)} lines')


def check_line_without_scene(session, check, line_a):
    session.ask_ok('pose 0 300 0 1.5708 1.2')
    reply = session.ask(f'restore {format_line(without(parse(line_a), "scene"))}')
    now = parse(session.ask_ok('describe')['line'])
    differing = [key for key in parse(line_a) if key != 'pt samples' and now.get(key) != parse(line_a)[key]]
    check('[G4] restore of A without scene, as a line from before scene files, restores it in the scene opened without -scene',
          reply['ok'] and not differing, reply.get('error') or ', '.join(differing) or 'same')


def check_notes(session, check, line_a, restored_b):
    note = restored_b.get('note', '')
    check('[G4] restore of a line with TAA on says its jitter cannot be reproduced', 'jitter' in note, note)
    scene = session.ask_ok('state ui')['state']['ui']['scene']
    current = f'{scene["width"]}x{scene["height"]}'
    other = '1600x900' if current != '1600x900' else '1280x720'
    reply = session.ask(f'restore {format_line(parse(line_a) | {"size": other})}')
    after = session.ask_ok('state ui')['state']['ui']['scene']
    check('[G4] restore of a line of another resolution restores the rest and notes both resolutions, keeping the scene size',
          reply['ok'] and other in reply.get('note', '') and current in reply.get('note', '') and after == scene,
          reply.get('note') or reply.get('error'))


def check_validation_line(session, check):
    view = session.ask_ok('state settings.Ray Tracing View')['state']['settings']['Ray Tracing View']['View']
    session.ask_ok('settings set Ray Tracing View: View=Validation: Depth Comparison')
    session.ask_ok(f'frames {VALIDATION_VIEW_SETTLE_FRAMES}')
    line = session.ask_ok('validation capture')['line']
    described = parse(session.ask_ok('describe')['line'])
    logged = parse(line.split('] ', 1)[1])
    missing = [key for key in described if key not in logged]
    check('[G3] the validation Log line has every condition describe gives, and its mode', not missing and 'mode' in logged,
          ', '.join(missing) or line)
    session.ask_ok('pose 0 300 0 1.5708 1.2')
    session.ask_ok(f'restore {line}')
    now = parse(session.ask_ok('describe')['line'])
    differing = [key for key in described if key != 'pt samples' and now.get(key) != logged.get(key)]
    check('[G3] restore of the validation Log line gives its conditions again', not differing, ', '.join(differing) or 'same')
    session.ask_ok(f'settings set Ray Tracing View: View={view}')
    session.ask_ok(f'frames {VALIDATION_VIEW_SETTLE_FRAMES}')


def second_run(check, line_a, image_a, state_a, line_b, state_b):
    instance = start_instance('Debug', build_first=False)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_coverage(check, session.ask_ok('describe')['line'], line_a, line_b)

            check_restored(session, check, line_a, state_a, 'A')
            session.ask_ok('settings set Path Tracer: Show=1')
            session.ask_ok(f'pt wait samples={SAMPLES}', timeout=WAIT_TIMEOUT)
            reply, image = export(session, 'line-a-restarted')
            check_export_line(session, check, reply, 'capture pt after the restart')
            same = np.array_equal(image.view(np.uint32), image_a.view(np.uint32))
            differing = int(np.count_nonzero(np.any(image.view(np.uint32) != image_a.view(np.uint32), axis=2))) if image.shape == image_a.shape else None
            check(f'[G2b] the export of A after the restart is bit-identical to the one before, at {SAMPLES} samples', same,
                  f'{differing} pixels differ' if differing is not None else f'shape {image.shape} vs {image_a.shape}')
            check('[G2b] the export of A after the restart leaves the same line as the one before', reply['line'] == line_a,
                  reply['line'] if reply['line'] == line_a else f'{reply["line"]} vs {line_a}')
            print(f'kept {keep(reply["path"])}')

            restored_b = check_restored(session, check, line_b, state_b, 'B')
            session.ask_ok(f'pt wait samples={LINE_B["samples"]}', timeout=WAIT_TIMEOUT)
            check_unreadable(session, check, line_a)
            check_line_without_scene(session, check, line_a)
            check_notes(session, check, line_a, restored_b)
            check_validation_line(session, check)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise


def command_check(args):
    check = Checks()
    try:
        second_run(check, *first_run(check, args.no_build))
    except (HarnessError, ImageFileError) as error:
        check('check ran to the end', False, str(error))
    print(f'kept exports: {KEPT_DIR}')
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('check', help='start Debug twice and judge the criteria')
    command.add_argument('--no-build', action='store_true')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
