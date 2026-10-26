"""PathTracer 의 확인 script 들이 같이 쓰는 판정, app 조작, 측정 기록.

script 마다 PASS/FAIL 의 형식, 종료 때의 판정, 측정 기록에 덧붙이는 방식이 달라지지 않도록 여기에 모은다.
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer import LIVE_DEVICE_ID, REPO_ROOT, HarnessError, describe_d3d12, stop_instance

RECORD = REPO_ROOT / 'docs' / 'reference-path-tracer-measurements.md'
SECTION = 'Path Tracer'

# furnace 는 Geometric Normals 를 켜면 분산이 0 이라 적은 sample 로 답을 본다.
FURNACE_SAMPLES = 16
# albedo 1 의 Sponza 에서 FURNACE_SAMPLES 의 path 가 모두 escape 하는 반사 횟수. Seed 0 에서 1.0 이 아닌 pixel 이 1024 번에서 5292 개,
# 4096 번에서 12 개, 16384 번에서 1 개였다.
WHITE_FURNACE_BOUNCES = 65536

BASE = {'Show': 1, 'Samples per Frame': 1, 'Seed': 0, 'Sky Radiance': 1, 'Max Bounces': 64, 'Russian Roulette': 1,
        'Geometric Normals': 0, 'Furnace': 0, 'Furnace Albedo': 1, 'Furnace Emission': 0}
WHITE_FURNACE = BASE | {'Furnace': 1, 'Furnace Albedo': 1, 'Furnace Emission': 0, 'Geometric Normals': 1,
                        'Max Bounces': WHITE_FURNACE_BOUNCES, 'Russian Roulette': 0}
# 해는 어디서나 emission / (1 - albedo) = Sky Radiance = 1 이다.
EMISSION_FURNACE = BASE | {'Furnace': 1, 'Furnace Albedo': 0.5, 'Furnace Emission': 0.5, 'Geometric Normals': 1, 'Max Bounces': 64,
                           'Russian Roulette': 0}


class Checks:
    def __init__(self):
        self.failures = []

    def __call__(self, name, passed, detail=''):
        print(f'{"PASS" if passed else "FAIL"}  {name}' + (f': {detail}' if detail != '' else ''), flush=True)
        if not passed:
            self.failures.append(name)
        return passed


def set_path_tracer(session, settings):
    return session.ask_ok(f'settings set {SECTION}: ' + '; '.join(f'{key}={value}' for key, value in settings.items()))


def path_tracer_state(session):
    return session.ask_ok('state pt')['state']['pt']


def stop_and_check(instance, check):
    """app 을 끝내고 정상 종료와 D3D12 message 를 판정한다. stop_instance 의 결과를 돌려준다."""
    stopped = stop_instance(instance)
    check('quit ends the app normally', stopped['result'] == 'quit' and stopped['exit_reason'] == 'normal',
          f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    d3d12 = stopped['d3d12']
    lines = '; '.join(describe_d3d12(d3d12))
    running = [message for message in d3d12['running'].values() if message['severity'] in ('CORRUPTION', 'ERROR', 'WARNING')]
    check('no D3D12 corruption, error or warning while running', d3d12['logged'] and not running, lines)
    at_exit = {id: message['count'] for id, message in (d3d12['at_exit'] or {}).items()}
    check(f'the D3D12 exit report has only one #{LIVE_DEVICE_ID} LIVE_DEVICE', at_exit == {LIVE_DEVICE_ID: 1}, lines)
    return stopped


def code_version():
    """잴 때의 HEAD. commit 하지 않은 변경이 있으면 + 를 붙인다."""
    def git(*arguments):
        return subprocess.run(['git', *arguments], cwd=REPO_ROOT, capture_output=True, text=True, check=True).stdout.strip()
    return git('rev-parse', '--short', 'HEAD') + ('+' if git('status', '--porcelain') else '')


def describe_machine(session):
    """측정 기록의 config, GPU, scene 열."""
    engine = session.ask_ok('state engine')['state']['engine']
    scene = session.ask_ok('state ui')['state']['ui']['scene']
    return [engine['config'], engine['gpu'], f'{scene["width"]}x{scene["height"]}']


def append_record_row(section, cells):
    """RECORD 의 section 에 있는 표의 끝에 한 줄을 더한다."""
    lines = RECORD.read_text(encoding='utf-8').splitlines(keepends=True)
    try:
        start = next(index for index, line in enumerate(lines) if line.rstrip() == section)
    except StopIteration:
        raise HarnessError(f'{RECORD} has no section "{section}"')
    end = next((index for index in range(start + 1, len(lines)) if lines[index].startswith('## ')), len(lines))
    table = [index for index in range(start + 1, end) if lines[index].startswith('|')]
    if not table:
        raise HarnessError(f'the section "{section}" of {RECORD} has no table')
    lines.insert(table[-1] + 1, '| ' + ' | '.join(cells) + ' |\n')
    RECORD.write_text(''.join(lines), encoding='utf-8')
