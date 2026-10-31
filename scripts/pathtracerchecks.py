"""PathTracer 의 확인 script 들이 같이 쓰는 설정, app 조작, 측정 기록.

script 마다 PathTracer 를 설정하는 값과 측정 기록에 덧붙이는 방식이 달라지지 않도록 여기에 모은다. PASS/FAIL 과 종료의 판정은
checks.py 에 있다.
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer import REPO_ROOT, HarnessError

RECORD = REPO_ROOT / 'docs' / 'reference-path-tracer-measurements.md'
# PT 의 설정은 Engine Tuning 의 Path Tracer 에 있다. settings 의 key 는 PT 의 설정 이름 앞에 KEY_PREFIX 를 붙인 것이다.
SECTION = 'Engine Tuning'
KEY_PREFIX = 'Path Tracer/'

# furnace 는 Geometric Normals 를 켜면 분산이 0 이라 적은 sample 로 답을 본다.
FURNACE_SAMPLES = 16
# albedo 1 의 Sponza 에서 FURNACE_SAMPLES 의 path 가 모두 escape 하는 반사 횟수. Seed 0 에서 1.0 이 아닌 pixel 이 1024 번에서 5292 개,
# 4096 번에서 12 개, 16384 번에서 1 개였다.
WHITE_FURNACE_BOUNCES = 65536

# Scene Hierarchy panel 과 editor 명령이 Sky Light 를 가리키는 이름
SKY_LIGHT_OBJECT = 'Sky Light'
# PT 가 miss 에서 받는 radiance 는 PT 의 설정이 아니라 scene 물체인 Sky Light 의 color 와 intensity 다. 이 둘도 PT 의 image 를 정하므로
# PT 의 설정과 같은 dict 에 '<물체>: <key>' 로 적고, set_path_tracer 가 그 물체에 넘긴다. 확인들은 흰 Sky Light 의 intensity 를 radiance 로 쓴다.
SKY_COLOR = f'{SKY_LIGHT_OBJECT}: Color'
SKY_RADIANCE = f'{SKY_LIGHT_OBJECT}: Intensity'

# 작업 5 ~ 7 의 기준과 측정 기록은 Lambert 로 정했으므로 Shading 을 Lambert 로 둔다. glTF BRDF 의 확인은 check-gltf-brdf.py 가 한다.
# 작업 5 ~ 10 의 기준은 Radiance 로 정했으므로 Output 을 Radiance 로 둔다. Indirect Irradiance 의 확인은 check-indirect-irradiance.py 가 한다.
BASE = {'Show': 1, 'Output': 'Radiance', 'Shading': 'Lambert', 'Samples per Frame': 1, 'Seed': 0, SKY_COLOR: '1,1,1', SKY_RADIANCE: 1,
        'Max Bounces': 64, 'Russian Roulette': 1, 'Geometric Normals': 0, 'Furnace': 0, 'Furnace Albedo': 1, 'Furnace Emission': 0}
WHITE_FURNACE = BASE | {'Furnace': 1, 'Furnace Albedo': 1, 'Furnace Emission': 0, 'Geometric Normals': 1,
                        'Max Bounces': WHITE_FURNACE_BOUNCES, 'Russian Roulette': 0}
# 해는 어디서나 emission / (1 - albedo) = Sky Light 의 radiance = 1 이다.
EMISSION_FURNACE = BASE | {'Furnace': 1, 'Furnace Albedo': 0.5, 'Furnace Emission': 0.5, 'Geometric Normals': 1, 'Max Bounces': 64,
                           'Russian Roulette': 0}


def path_tracer_settings(values):
    """Engine Tuning 의 값 중 PT 의 설정을 KEY_PREFIX 를 뗀 이름으로 돌려준다."""
    return {key[len(KEY_PREFIX):]: value for key, value in values.items() if key.startswith(KEY_PREFIX)}


def set_path_tracer(session, settings):
    """settings 의 '<물체>: <key>' 는 scene 의 그 물체에, 나머지는 PT 의 설정에 넣고, PT 의 설정을 넣었으면 넣은 뒤의 PT 의 설정을 돌려준다."""
    sections = {}
    for key, value in settings.items():
        section, separator, name = key.partition(': ')
        if not separator:
            section, name = SECTION, KEY_PREFIX + key
        sections.setdefault(section, {})[name] = value
    replies = {section: session.ask_ok(f'{"settings" if section == SECTION else "editor"} set {section}: '
                                       + '; '.join(f'{key}={value}' for key, value in values.items()))
               for section, values in sections.items()}
    return path_tracer_settings(replies[SECTION]['state']['settings'][SECTION]) if SECTION in replies else None


def light_by_sky_only(session):
    """directional light 를 끈다. 작업 5, 6 의 기준과 측정 기록은 sky 만 비추는 장면에서 정했다. furnace 는 light 를 스스로 끈다."""
    return session.ask_ok('light set intensity=0')


def path_tracer_state(session):
    return session.ask_ok('state pt')['state']['pt']


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
