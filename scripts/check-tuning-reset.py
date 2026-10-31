"""Engine Tuning 의 설정을 기본값으로 되돌리면 되돌린 설정만 기본값이 되고 나머지는 그대로인지 확인한다.

  python scripts/check-tuning-reset.py check   # Debug 로 세 번 띄워 아래 기준을 모두 판정한다

설정은 `settings reset Engine Tuning: <path>` 명령으로 되돌린다. 이 명령과 Engine Tuning panel 의 Reset to Default, Reset to Defaults,
Reset All 은 EngineTuning::ResetToDefaults 를 함께 쓴다. panel 의 우클릭 menu 와 button 은 computer use 로 본다. app 을 직접 띄우고
끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION,
ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다.

기본값 D 는 harness baseline 의 user.ini 에서 [Engine Tuning] 을 뺀 user.ini 로 띄운 app 의 state settings.Engine Tuning 이다.
HM 은 [Engine Tuning] 을 뺀 user.ini 에 Ray Tracing/View=VALIDATION_VIEW 와 Ray Tracing/Heatmap=HEATMAP_METRIC 만 넣어 띄운 app 의
Heatmap Range 다. 기준은 harness baseline 으로 띄운 app 에서 판정한다. 아래의 "그대로" 는 reset 전과 문자열로 같다는 뜻이고,
Engine Tuning 의 다른 key 와 다른 section 을 모두 본다.

  [T1] CHANGES 로 바꾼 key 를 하나씩 reset 하면 reset 한 key 만 D 의 값이 되고 나머지는 그대로다. CHANGES 는 EngineVar 의 type 마다 하나와
       Path Tracer, Ray Tracing section 의 key 다. 이미 D 의 값인 UNCHANGED_KEY 를 reset 해도 모두 그대로다.
  [T2] CHANGES 로 바꾼 뒤 GROUPS 의 group 을 하나씩 reset 하면 group 아래 key 가 모두 D 의 값이 되고 나머지는 그대로다. GROUPS 는
       EngineVar 의 group 하나와 section 이 붙은 group 하나다.
  [T3] VALIDATION_VIEW 를 고른 직후의 Ray Tracing/Heatmap 과 Heatmap Range 를 H0 라 한다. Heatmap Range 를 바꾸고 reset 하면 H0 다.
       Heatmap 을 HEATMAP_METRIC 으로 두고 Heatmap Range 를 바꾼 뒤 Heatmap Range 를 reset 하면 Heatmap 은 그대로이고 Heatmap Range
       는 HM 이다. 이어서 Heatmap Range 를 다시 바꾸고 Heatmap 을 reset 하면 H0 다. View 를 reset 하면 Ray Tracing 의 key 가 D 와
       같다 (Heatmap 과 Heatmap Range 가 없다). reset 마다 Heatmap 과 Heatmap Range (View 의 reset 은 View 도) 밖은 그대로다.
  [T4] CHANGES 와 VALIDATION_VIEW, HEATMAP_METRIC, 바꾼 Heatmap Range 를 둔 뒤 빈 path 로 reset 하면 settings.Engine Tuning 이 D 와 같고
       다른 section 은 그대로다.
  [T5] NO_SUCH_PATHS 의 reset 과 Engine Tuning 이 아닌 OTHER_SECTIONS 의 reset 은 error 이고 settings 가 모두 그대로다.
  [K1] harness baseline 으로 띄운 직후, baseline user.ini 의 key 가 모두 state settings 에서 user.ini 의 값과 같고, user.ini 에 없는
       Engine Tuning 의 key 는 D 와 같다. 기본값을 기억하는 code 가 user.ini 를 되돌리는 순서 안에 있으므로 되돌리기가 그대로인지 본다.
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from viewer import BASELINE_DIR, HARNESS_DIR, HarnessError, Session, start_instance, stop_instance

SECTION = 'Engine Tuning'
WORK_DIR = HARNESS_DIR / 'tuning-reset-check'
# EngineVar 의 BoolVar, NumVar, ExpVar, IntVar, EnumVar 와 section 의 key. 값은 모두 D 와 다르다 ([T1] 이 먼저 확인한다).
CHANGES = {
    'Graphics/Bloom/Enable': 'off',
    'Graphics/AA/TAA/Sharpness': '0.25',
    'Graphics/AA/TAA/Speed Limit': '16',
    'Graphics/SSAO/Hierarchy Depth': '2',
    'Graphics/SSAO/Quality Level': 'Low',
    'Path Tracer/Seed': '7',
    'Path Tracer/Shading': 'Lambert',
    'Path Tracer/Furnace Albedo': '0.5',
    'Ray Tracing/Show Single-Sided Back Faces': '1',
}
UNCHANGED_KEY = 'Graphics/Bloom/Strength'
# Graphics/AA 에는 harness baseline 이 끈 TAA 도 있다.
GROUPS = ['Graphics/AA', 'Path Tracer']
VALIDATION_VIEW = 'Validation: Depth Comparison'
HEATMAP_METRIC = 'Depth Error'
# H0 와 HM 의 범위와 다르다 ([T3] 이 먼저 확인한다).
CHANGED_RANGES = ['-12,-6', '-10,-5']
VIEW = 'Ray Tracing/View'
HEATMAP = 'Ray Tracing/Heatmap'
HEATMAP_RANGE = 'Ray Tracing/Heatmap Range'
# key 이름의 앞부분만 맞는 path 와, '/' 로 나뉜 이름이 하나도 맞지 않는 path
NO_SUCH_PATHS = ['Graphics/A', 'Graphics/AA/TAA/Enabl', 'Path Tracer/Se', 'No Such Group']
OTHER_SECTIONS = ['Editor Camera: Move Speed', 'Panels: Profiler']


def read_ini(text):
    sections, current = {}, None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            current = sections.setdefault(line[1:-1], {})
        elif current is not None and '=' in line and not line.startswith(';'):
            key, value = line.split('=', 1)
            current[key.strip()] = value.strip()
    return sections


def baseline_without_tuning(name, tuning_lines):
    """harness baseline 의 user.ini 에서 [Engine Tuning] 을 tuning_lines 로 바꾼 baseline 폴더"""
    folder = WORK_DIR / name
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for ini in Path(BASELINE_DIR).glob('*.ini'):
        shutil.copy2(ini, folder / ini.name)
    text = (folder / 'user.ini').read_text(encoding='utf-8')
    replacement = f'[{SECTION}]\n' + ''.join(f'{line}\n' for line in tuning_lines) + '\n' if tuning_lines else ''
    text = re.sub(r'\[Engine Tuning\]\n.*?(?=\n\[|\Z)\n?', replacement, text, flags=re.S)
    (folder / 'user.ini').write_text(text, encoding='utf-8')
    return folder


def settings_of(session):
    return session.ask_ok('state settings')['state']['settings']


def run_for_state(check, name, baseline, build_first):
    """baseline 으로 띄운 직후의 state settings.Engine Tuning"""
    instance = start_instance('Debug', baseline=baseline, build_first=build_first)
    try:
        session = Session(instance)
        try:
            tuning = settings_of(session)[SECTION]
        finally:
            session.close()
        stop_and_check(instance, check)
    except HarnessError:
        stop_instance(instance, kill_now=True)
        raise
    print(f'read {name}')
    return tuning


def is_under(key, path):
    return path == '' or key == path or key.startswith(path + '/')


def describe_differences(first, second):
    keys = sorted(set(first) | set(second))
    return '; '.join(f'{key}: {first.get(key, "-")} vs {second.get(key, "-")}' for key in keys if first.get(key) != second.get(key))


def check_others_kept(check, name, before, after, affected):
    """affected 가 아닌 Engine Tuning key 와 다른 section 이 그대로인지 판정한다."""
    outside = lambda settings: {key: value for key, value in settings[SECTION].items() if not affected(key)}
    others = lambda settings: {section: values for section, values in settings.items() if section != SECTION}
    differences = describe_differences(outside(before), outside(after))
    other_differences = '; '.join(f'[{section}] {describe_differences(before[section], after.get(section, {}))}'
                                  for section in others(before) if before[section] != after.get(section))
    check(name, not differences and not other_differences and others(before).keys() == others(after).keys(),
          '; '.join(text for text in [differences, other_differences] if text))


def reset(session, path):
    return session.ask_ok(f'settings reset {SECTION}: {path}')


def set_tuning(session, values):
    session.ask_ok(f'settings set {SECTION}: ' + '; '.join(f'{key}={value}' for key, value in values.items()))


def check_loaded(check, settings, defaults):
    baseline = read_ini((Path(BASELINE_DIR) / 'user.ini').read_text(encoding='utf-8'))
    expected = {section: dict(values) for section, values in baseline.items()}
    expected[SECTION] = defaults | baseline.get(SECTION, {})
    differences = '; '.join(f'[{section}] {describe_differences(values, {key: settings.get(section, {}).get(key) for key in values})}'
                            for section, values in expected.items()
                            if any(settings.get(section, {}).get(key) != value for key, value in values.items()))
    unexpected = set(settings[SECTION]) - set(expected[SECTION])
    check('[K1] the baseline user.ini is loaded over the defaults', not differences and not unexpected,
          differences + (f'; keys that are not defaults: {sorted(unexpected)}' if unexpected else ''))


def check_errors(session, check):
    before = settings_of(session)
    for line in [f'settings reset {SECTION}: {path}' for path in NO_SUCH_PATHS] + [f'settings reset {other}' for other in OTHER_SECTIONS]:
        reply = session.ask(line)
        check(f'[T5] {line} is an error', not reply['ok'], reply.get('error', ''))
    after = settings_of(session)
    check('[T5] the refused resets change no setting', before == after, describe_differences(before[SECTION], after[SECTION]))


def check_item_resets(session, check, defaults):
    at_default = {key: value for key, value in CHANGES.items() if defaults.get(key) == value}
    check('[T1] CHANGES differ from the defaults and UNCHANGED_KEY is at its default',
          not at_default and UNCHANGED_KEY in defaults, f'at the default: {at_default}' if at_default else '')
    set_tuning(session, CHANGES)
    for key in list(CHANGES) + [UNCHANGED_KEY]:
        before = settings_of(session)
        reply = reset(session, key)
        after = settings_of(session)
        check(f'[T1] reset {key} sets it to {defaults[key]}', after[SECTION].get(key) == defaults[key] and reply['reset'] == 1,
              f'{before[SECTION].get(key)} -> {after[SECTION].get(key)}, reset {reply["reset"]}')
        check_others_kept(check, f'[T1] reset {key} keeps the others', before, after, lambda other: other == key)


def check_group_resets(session, check, defaults):
    set_tuning(session, CHANGES)
    for group in GROUPS:
        before = settings_of(session)
        reply = reset(session, group)
        after = settings_of(session)
        under = lambda values: {key: value for key, value in values.items() if is_under(key, group)}
        changed = describe_differences(under(before[SECTION]), under(defaults))
        check(f'[T2] {group} had settings off their defaults', changed != '', changed)
        differences = describe_differences(under(after[SECTION]), under(defaults))
        check(f'[T2] reset {group} sets every setting under it to its default', not differences and reply['reset'] == len(under(defaults)),
              differences or f'reset {reply["reset"]} of {len(under(defaults))}')
        check_others_kept(check, f'[T2] reset {group} keeps the others', before, after, lambda key: is_under(key, group))


def heatmap_of(settings):
    return settings[SECTION].get(HEATMAP), settings[SECTION].get(HEATMAP_RANGE)


def check_heatmap_resets(session, check, defaults, metric_range):
    set_tuning(session, {VIEW: VALIDATION_VIEW})
    start = settings_of(session)
    first = heatmap_of(start)
    check('[T3] CHANGED_RANGES differ from H0 and HM', all(changed not in (first[1], metric_range) for changed in CHANGED_RANGES) and
          first[0] != HEATMAP_METRIC, f'H0 {first}, HM {metric_range}')

    def reset_and_check(name, path, expected):
        before = settings_of(session)
        reset(session, path)
        after = settings_of(session)
        check(f'[T3] {name}', heatmap_of(after) == expected, f'{heatmap_of(before)} -> {heatmap_of(after)}, expected {expected}')
        check_others_kept(check, f'[T3] reset {path} keeps the others', before, after, lambda key: key in (HEATMAP, HEATMAP_RANGE))

    set_tuning(session, {HEATMAP_RANGE: CHANGED_RANGES[0]})
    reset_and_check('reset Heatmap Range sets the range of the first heatmap of the view', HEATMAP_RANGE, first)
    set_tuning(session, {HEATMAP: HEATMAP_METRIC})
    set_tuning(session, {HEATMAP_RANGE: CHANGED_RANGES[1]})
    reset_and_check(f'reset Heatmap Range keeps {HEATMAP_METRIC} and sets its default range', HEATMAP_RANGE, (HEATMAP_METRIC, metric_range))
    set_tuning(session, {HEATMAP_RANGE: CHANGED_RANGES[0]})
    reset_and_check('reset Heatmap sets the first heatmap of the view and its range', HEATMAP, first)

    view_keys = (VIEW, HEATMAP, HEATMAP_RANGE)
    before = settings_of(session)
    reset(session, VIEW)
    after = settings_of(session)
    view = lambda values: {key: value for key, value in values.items() if key in view_keys}
    differences = describe_differences(view(after[SECTION]), view(defaults))
    check('[T3] reset View sets the view and its heatmap to the defaults', not differences, differences)
    check_others_kept(check, f'[T3] reset {VIEW} keeps the others', before, after, lambda key: key in view_keys)


def check_reset_all(session, check, defaults):
    set_tuning(session, CHANGES | {VIEW: VALIDATION_VIEW})
    set_tuning(session, {HEATMAP: HEATMAP_METRIC})
    set_tuning(session, {HEATMAP_RANGE: CHANGED_RANGES[0]})
    before = settings_of(session)
    reply = reset(session, '')
    after = settings_of(session)
    differences = describe_differences(after[SECTION], defaults)
    check('[T4] reset of the empty path sets every setting to its default', not differences, differences or f'reset {reply["reset"]}')
    check_others_kept(check, '[T4] reset of the empty path keeps the other sections', before, after, lambda key: True)


def check_resets(check, defaults, metric_range):
    instance = start_instance('Debug', build_first=False)
    try:
        session = Session(instance)
        try:
            check_loaded(check, settings_of(session), defaults)
            check_errors(session, check)
            check_item_resets(session, check, defaults)
            check_group_resets(session, check, defaults)
            check_heatmap_resets(session, check, defaults, metric_range)
            check_reset_all(session, check, defaults)
        finally:
            session.close()
        stop_and_check(instance, check)
    except HarnessError:
        stop_instance(instance, kill_now=True)
        raise


def command_check(args):
    check = Checks()
    try:
        defaults = run_for_state(check, 'the defaults', baseline_without_tuning('defaults', []), not args.no_build)
        metric = run_for_state(check, f'the range of {HEATMAP_METRIC}',
                               baseline_without_tuning('heatmap', [f'{VIEW}={VALIDATION_VIEW}', f'{HEATMAP}={HEATMAP_METRIC}']), False)
        check(f'[T3] the app started with {HEATMAP_METRIC} has it', metric.get(HEATMAP) == HEATMAP_METRIC, str(metric.get(HEATMAP)))
        check_resets(check, defaults, metric.get(HEATMAP_RANGE))
    except HarnessError as error:
        check('check ran to the end', False, str(error))
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('check', help='start Debug three times and judge the criteria')
    command.add_argument('--no-build', action='store_true')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
