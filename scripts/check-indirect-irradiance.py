"""PathTracer 의 Indirect Irradiance 출력이 정의대로 쌓이는지 판정한다.

  python scripts/check-indirect-irradiance.py check   # Debug 로 띄워 아래 기준을 판정한다
  python scripts/check-indirect-irradiance.py keep    # Profile 로 띄워 발표에 쓸 image 를 KEPT_DIR 에 남긴다

번호는 작업 11 의 완료 기준의 번호다. Indirect Irradiance 의 정의와 단위는 PathTracer.h 의 Output 에 있다. 이 script 밖에서 보는 기준:
  [U1] 의 저장과 reset: check-lambert-path.py check 의 [8], [9]
  [U2], [U3] 측정 조건의 줄: check-log-restore.py check 의 [G1], [G2b], [G4]
  [U4] panel 의 Output combo: 사람이나 computer use 로 바꾸고 그 전후의 state 로 본다

app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12
CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다. PT 의 값은 capture pt 의 export 로 본다. export 의
rgb 는 E (lux) 의 평균이고 a 는 더한 sample 수다. hit pixel 은 Ray Tracing View 의 Front Face capture 에서 면을 만난 pixel 이다. 따로
적지 않으면 harness baseline 의 시점, Shading Lambert, Geometric Normals on, Russian roulette off, FURNACE_SAMPLES 다.

furnace: Russian roulette 를 끄면 RNG 를 쓰는 양이 albedo, emission, sky 에 따라 달라지지 않으므로, Seed 와 Max Bounces 가 같은 furnace
들은 같은 path 를 그린다 (check-path-tracer-export.py). 그래서 [I2] 의 예측은 기대값이 아니라 sample 마다 성립한다.
  [I1] white furnace (pathtracerchecks.WHITE_FURNACE) 를 Russian roulette off 와 on 으로 쌓는다. sample 하나는 Max Bounces 안에 escape
       하면 π 이고 못하면 0 이므로, 모든 hit pixel 의 E/π 가 k / FURNACE_SAMPLES (k 는 escape 한 sample 수) 와 FURNACE_TOLERANCE 안에서
       맞고, k 가 FURNACE_SAMPLES 보다 작은 pixel 이 MAX_TRAPPED_PIXELS 이하다. x₁ 에 곱하는 π 와 cosine sampling 의 정규화를 본다.
       작업 11 을 판정할 때 Seed 0 에서 off 와 on 각각 한 pixel 이 16 개 중 1 개의 sample 을 escape 하지 못했다. Radiance 의 white
       furnace 와 달리 한 pixel 의 sample 이 모두 같은 x₁ 에서 출발하므로, x₁ 이 path 가 잘 빠져나가지 못하는 곳이면 그 pixel 의
       sample 이 갇힐 수 있다 (추측).
  [I2] emission furnace (pathtracerchecks.EMISSION_FURNACE) 를 B in EMISSION_FURNACE_BOUNCES 로 쌓는다. B = 0 이면 모든 hit pixel 이 0
       이다. B >= 1 이면 모든 hit pixel 이 p = π (1 - 0.5^B (1 - E_WF(B)/π)) 와 상대 FURNACE_TOLERANCE 안에서 맞는다. E_WF(B) 는 같은
       B 의 white furnace 다. x₁ 에서 모은 path 는 x₂ 부터 B - 1 번 더 반사하므로, B 번 안에 escape 하지 못한 path 의 E/π 는 1 - 0.5^B
       다. Radiance 의 식 (check-path-tracer-export.py 의 [F2]) 은 지수가 B + 1 이다. 지수가 B 이면 x₁ 의 emission 을 빼고 x₁ 에서
       모으는 일을 반사 하나로 센다는 뜻이다.
  [I5] Geometric Normals 를 끈 white furnace 에서 모든 hit pixel 이 E/π <= 1 + FURNACE_TOLERANCE 다. shading normal 이 energy 를 만들지
       않는 것을 본다. 평균 손실을 찍는다.
  [I6] glTF 의 반사: EMISSION_FURNACE 를 Shading glTF 로 Seed 0, 1 씩 쌓는다. hit pixel 에서 상수 π 를 B 로 준 imagecompare 의 r 이
       두 Seed 를 각각 A0 로 둘 때 모두 AGREEMENT_LIMIT 이하이고, NaN 이나 Inf 로 버린 sample 이 0 이다. Russian roulette off 와 on 을
       본다. glTF 의 반사마다의 weight 는 albedo 둘레에서 흔들리므로 [I2] 처럼 sample 마다 맞지 않는다.

light (furnace off, light 는 harness baseline 의 것, Geometric Normals off):
  [I3] Sky 0, Max Bounces 1 이면 모든 pixel 이 0 이다. x₁ 이 직접 받는 light 를 더하지 않는다는 뜻이다. Max Bounces 2 면 0 이 아닌
       pixel 이 있다. Shading glTF 와 Lambert 모두 본다. Sponza 에는 emission 이 없다.
  [I4] Sky 1 에서 같은 Seed 로 Max Bounces 를 directlightchecks.BOUNCE_STEPS 로 올리면 어떤 pixel 도 sample 수만큼의 ulp 보다 더
       어두워지지 않고 평균은 매번 커진다. check-directional-light.py 의 [P7] 과 같은 판정이다. glTF 와 Lambert 모두 본다.

SCENE (glTF, light, Sky 1, Geometric Normals off, Max Bounces 64, Russian roulette on):
  [I7] viewer.SNAPSHOT_POSES 의 시점마다 SCENE_SAMPLES 만큼 쌓으면, a 가 0 인 pixel 이 Front Face capture 의 miss pixel 과 같고 그
       rgb 는 0 이며, 나머지 pixel 의 a 는 pt.samples 다. x₁ 이 Front Face view 와 같은 ray (TAA 를 끈 raster sample 위치) 로 만난 면이고,
       hit pixel 에 NaN 이나 Inf 로 버린 sample 이 없다는 뜻이다. courtyard-up 에는 miss pixel 이 있어야 한다.
  [I8] courtyard-up 에서 scene capture 가 hit pixel 에서 export 의 rgb/π 와 R11G11B10 의 1 ulp 안이고 miss pixel 에서 0 이다. 식은
       check-path-tracer-export.py 의 [E2] 와 같다.
  [I9] SCENE 을 REPRODUCTION_SAMPLES 만큼 쌓은 export, Seed 1 로 바꿨다가 되돌려 다시 쌓은 export, Samples per Frame 4 로 쌓은 export,
       TAA 를 켜고 쌓은 export 가 rgba 의 bit 까지 같다. PT 는 TAA jitter 와 무관하게 pixel 격자의 중심을 쏘기 때문이다.

설정:
  [U1] harness baseline 의 editor.ini 에는 Output 이 없고, 띄운 직후 state 의 Output 이 Radiance 다.

keep 은 harness baseline 의 시점에서 KEEP 을 Output Indirect Irradiance 의 glTF 와 Lambert, Output Radiance 의 glTF 로 KEEP_SAMPLES 만큼
쌓아 KEPT_DIR 에 DDS 와 보기용 PNG 로 남긴다. Indirect Irradiance 의 PNG 는 E/π 를 Radiance 와 같은 exposure 로 보인다. 그림자 경계와
x₁ 의 texture 가 Indirect Irradiance 에서 사라지는지는 그 PNG 를 사람이 본다 ([D3]).
"""

import argparse
import configparser
import json
import math
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from directlightchecks import check_bounces
from imagecompare import agreement, rmse
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import (BASE, EMISSION_FURNACE, FURNACE_SAMPLES, SECTION, SKY_RADIANCE, WHITE_FURNACE, path_tracer_state,
                              set_path_tracer)
from viewer import (BASELINE_DIR, HARNESS_DIR, PREVIEW_EXPOSURE, SETTLE_FRAMES, SNAPSHOT_POSES, HarnessError, Session, default_scene_pose, capture,
                    make_view, shots_dir, start_instance, stop_instance)

KEPT_DIR = HARNESS_DIR / 'kept' / 'indirect-irradiance'

RADIANCE = {'Output': 'Radiance'}
IRRADIANCE = {'Output': 'Indirect Irradiance'}
GLTF = {'Shading': 'glTF'}

FURNACE_TOLERANCE = 1e-4
# [I1] 에서 escape 하지 못한 sample 이 있어도 되는 pixel 수. 판정할 때 잰 1 개에 여유를 둔다.
MAX_TRAPPED_PIXELS = 4
EMISSION_FURNACE_BOUNCES = [0, 1, 2, 4, 64]
# r 은 약 200 만 pixel 의 합이라 bias 가 없으면 1 근처에 모인다. imagecompare.py selftest 가 그 흔들림을 본다.
AGREEMENT_LIMIT = 1.01
# glTF 의 furnace 가 상수와 이만큼 가까우면 r 의 분모가 0 에 가까워 r 이 뜻을 잃으므로 r 과 무관하게 통과다 (check-gltf-brdf.py 의 [S1]).
EXACT_RMSE = 1e-6
LIGHT_SAMPLES = 4

SCENE = BASE | GLTF | IRRADIANCE | {'Geometric Normals': 0, 'Max Bounces': 64, 'Russian Roulette': 1}
SCENE_SAMPLES = 256
REPRODUCTION_SAMPLES = 64
SAMPLES_BEFORE_RETURN = 8
TAA = 'Graphics/AA/TAA/Enable'
MISS_POSE = 'courtyard-up'

R11G11B10_MANTISSA_BITS = np.array([6, 6, 5])
R11G11B10_SMALLEST_NORMAL = 2.0 ** -14

KEEP = SCENE
KEEP_SAMPLES = 1024
KEEP_RUNS = {'irradiance-gltf': IRRADIANCE | GLTF, 'irradiance-lambert': IRRADIANCE | {'Shading': 'Lambert'}, 'radiance-gltf': RADIANCE | GLTF}

# 가장 느린 run 은 Max Bounces 65536 의 white furnace 다.
WAIT_TIMEOUT = 900


def export(session, name, settings, samples):
    """settings 로 처음부터 samples 만큼 쌓은 export 의 (rgba, 그 frame 의 pt.samples, DDS 경로)."""
    set_path_tracer(session, settings | {'Target Samples': samples})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={samples}', timeout=WAIT_TIMEOUT)
    reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / name}')
    state = json.loads(Path(reply['state']).read_text(encoding='utf-8'))
    return read_dds_rgba32f(reply['path']), state['pt']['samples'], Path(reply['path'])


def front_face_hits(session, name):
    """Front Face view 는 miss 를 검정으로 그린다."""
    session.ask_ok('settings set Ray Tracing View: View=Debug: Front Face')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    image = np.asarray(Image.open(capture(session, 'overlay', name)).convert('RGB'))
    session.ask_ok('settings set Ray Tracing View: View=Off')
    return np.any(image != 0, axis=2)


def irradiance_over_pi(image):
    return np.asarray(image[..., :3], dtype=np.float64) / math.pi


def same_bits(a, b):
    return bool(np.array_equal(a.view(np.uint32), b.view(np.uint32)))


def count(mask):
    return int(np.count_nonzero(mask))


def check_default_output(session, check):
    ini = configparser.ConfigParser(delimiters=('=',), comment_prefixes=(';',), interpolation=None)
    ini.optionxform = str
    ini.read(BASELINE_DIR / 'editor.ini', encoding='utf-8')
    in_baseline = ini.has_section(SECTION) and 'Output' in ini[SECTION]
    output = session.ask_ok(f'state settings.{SECTION}')['state']['settings'][SECTION].get('Output')
    check('[U1] the harness baseline has no Output and the path tracer starts with Radiance', not in_baseline and output == 'Radiance',
          f'Output in the baseline editor.ini: {in_baseline}; state Output {output}')


def check_furnaces(session, check, hits):
    for russian_roulette in (0, 1):
        image, _, _ = export(session, f'white-furnace-rr{russian_roulette}', WHITE_FURNACE | IRRADIANCE | {'Russian Roulette': russian_roulette},
                             FURNACE_SAMPLES)
        escaped = irradiance_over_pi(image)[hits] * FURNACE_SAMPLES
        deviation = float(np.max(np.abs(escaped - np.rint(escaped)))) / FURNACE_SAMPLES
        trapped = count(np.any(np.rint(escaped) < FURNACE_SAMPLES, axis=-1))
        check(f'[I1] white furnace with Russian roulette {"on" if russian_roulette else "off"} is pi x (escaped samples) / {FURNACE_SAMPLES} '
              f'within {FURNACE_TOLERANCE:g}, with samples trapped in {MAX_TRAPPED_PIXELS} pixels or fewer',
              deviation <= FURNACE_TOLERANCE and trapped <= MAX_TRAPPED_PIXELS, f'max deviation of E/pi {deviation:.3g}, {trapped} pixels with trapped samples')

    details, passed = [], True
    for bounces in EMISSION_FURNACE_BOUNCES:
        white, _, _ = export(session, f'white-furnace-b{bounces}', WHITE_FURNACE | IRRADIANCE | {'Max Bounces': bounces}, FURNACE_SAMPLES)
        emission, _, _ = export(session, f'emission-furnace-b{bounces}', EMISSION_FURNACE | IRRADIANCE | {'Max Bounces': bounces}, FURNACE_SAMPLES)
        escaped = irradiance_over_pi(white)[hits]
        measured = irradiance_over_pi(emission)[hits]
        if bounces == 0:
            nonzero = count(measured != 0.0)
            passed = passed and nonzero == 0
            details.append(f'B 0: {nonzero} values not 0')
        else:
            predicted = 1.0 - 0.5 ** bounces * (1.0 - escaped)
            deviation = float(np.max(np.abs(measured / predicted - 1.0)))
            passed = passed and deviation <= FURNACE_TOLERANCE
            details.append(f'B {bounces}: max relative {deviation:.3g}, mean E_WF/pi {float(escaped.mean()):.4g}')
    check(f'[I2] emission furnace is 0 for B 0 and matches pi (1 - 0.5^B (1 - E_WF(B)/pi)) within {FURNACE_TOLERANCE:g} for B in '
          f'{EMISSION_FURNACE_BOUNCES[1:]}', passed, ', '.join(details))

    image, _, _ = export(session, 'white-furnace-shading-normals', WHITE_FURNACE | IRRADIANCE | {'Geometric Normals': 0}, FURNACE_SAMPLES)
    values = irradiance_over_pi(image)[hits]
    check(f'[I5] white furnace with shading normals is {1 + FURNACE_TOLERANCE:g} pi or less everywhere', values.max() <= 1.0 + FURNACE_TOLERANCE,
          f'max E/pi {values.max():.9g}, mean loss {1.0 - values.mean():.4g}')


def check_gltf_furnace(session, check, hits):
    for russian_roulette in (0, 1):
        settings = EMISSION_FURNACE | GLTF | IRRADIANCE | {'Russian Roulette': russian_roulette}
        images, invalid = [], 0
        for seed in (0, 1):
            image, dispatched, _ = export(session, f'gltf-emission-furnace-rr{russian_roulette}-seed{seed}', settings | {'Seed': seed},
                                          FURNACE_SAMPLES)
            images.append(image[hits])
            invalid += int(np.sum(dispatched - image[hits][..., 3], dtype=np.int64))
        expected = np.full_like(images[0], math.pi)
        orders = [(agreement(images[first], images[1 - first], expected, expected).ratio, rmse(images[first], expected)) for first in (0, 1)]
        means = [float(image[..., :3].mean()) / math.pi for image in images]
        check(f'[I6] the glTF emission furnace with Russian roulette {"on" if russian_roulette else "off"} has r <= {AGREEMENT_LIMIT:g} '
              f'against pi with either Seed as A0',
              all(error <= EXACT_RMSE * math.pi or ratio <= AGREEMENT_LIMIT for ratio, error in orders) and invalid == 0,
              f'r {orders[0][0]:.5g} with Seed 0 as A0, {orders[1][0]:.5g} with Seed 1; mean E/pi {means[0]:.6f}, {means[1]:.6f}; '
              f'{invalid} invalid samples')


def check_direct_light_excluded(session, check, start_pose):
    session.ask_ok(f'pose {start_pose}')
    for shading in ('glTF', 'Lambert'):
        settings = BASE | IRRADIANCE | {'Shading': shading, SKY_RADIANCE: 0, 'Geometric Normals': 0, 'Russian Roulette': 0}
        one, _, _ = export(session, f'direct-b1-{shading.lower()}', settings | {'Max Bounces': 1}, LIGHT_SAMPLES)
        two, _, _ = export(session, f'direct-b2-{shading.lower()}', settings | {'Max Bounces': 2}, LIGHT_SAMPLES)
        lit_one = count(np.any(one[..., :3] != 0.0, axis=2))
        lit_two = count(np.any(two[..., :3] != 0.0, axis=2))
        check(f'[I3] {shading}: with Sky 0, Max Bounces 1 is 0 everywhere and Max Bounces 2 is not', lit_one == 0 and lit_two > 0,
              f'pixels not 0: {lit_one} with Max Bounces 1, {lit_two} with Max Bounces 2')


def check_reflections(session, check, start_pose):
    for shading in ('glTF', 'Lambert'):
        check_bounces(session, check, start_pose, f'[I4] {shading}:',
                      IRRADIANCE | {'Shading': shading, SKY_RADIANCE: 1, 'Geometric Normals': 0, 'Target Samples': LIGHT_SAMPLES})


def check_scene_capture(session, check, exported, hits):
    q = np.asarray(read_dds_rgba32f(capture(session, 'scene', 'scene-after-export'))[..., :3], dtype=np.float64)
    v = irradiance_over_pi(exported)
    tolerance = np.ldexp(1.0, -R11G11B10_MANTISSA_BITS) * np.maximum(v, R11G11B10_SMALLEST_NORMAL)
    outside = count(np.any(np.abs(q - v) > tolerance, axis=2) & hits)
    lit_misses = count(np.any(q != 0.0, axis=2) & ~hits)
    check(f'[I8] {MISS_POSE}: the scene capture is E/pi within one R11G11B10 ulp on hit pixels and 0 on misses', outside == 0 and lit_misses == 0,
          f'{outside} hit pixels outside, {lit_misses} miss pixels not 0')


def check_scene(session, check, start_pose):
    for pose_name, pose in SNAPSHOT_POSES.items():
        session.ask_ok(f'pose {pose or start_pose}')
        image, dispatched, _ = export(session, f'scene-{pose_name}', SCENE, SCENE_SAMPLES)
        hits = front_face_hits(session, f'front-face-{pose_name}')
        if pose_name == MISS_POSE:
            check_scene_capture(session, check, image, hits)
        added = image[..., 3]
        short = count(hits & (added != dispatched))
        counted_misses = count(~hits & (added != 0.0))
        lit_misses = count(~hits & np.any(image[..., :3] != 0.0, axis=2))
        enough_misses = pose_name != MISS_POSE or count(~hits) > 0
        check(f'[I7] {pose_name}: a is 0 exactly on the Front Face misses, which are 0, and pt.samples elsewhere',
              short == 0 and counted_misses == 0 and lit_misses == 0 and enough_misses,
              f'{count(~hits)} misses; {short} hit pixels with a != {dispatched}, {counted_misses} misses with a != 0, '
              f'{lit_misses} misses with rgb != 0')


def check_reproduction(session, check, start_pose):
    session.ask_ok(f'pose {start_pose}')
    first, _, _ = export(session, 'reproduction-first', SCENE, REPRODUCTION_SAMPLES)

    set_path_tracer(session, {'Seed': 1})
    session.ask_ok(f'pt wait samples={SAMPLES_BEFORE_RETURN}', timeout=WAIT_TIMEOUT)
    set_path_tracer(session, {'Seed': 0})
    session.ask_ok('frames 2')
    session.ask_ok(f'pt wait samples={REPRODUCTION_SAMPLES}', timeout=WAIT_TIMEOUT)
    reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / "reproduction-returned"}')
    returned = read_dds_rgba32f(reply['path'])

    paced, _, _ = export(session, 'reproduction-paced', SCENE | {'Samples per Frame': 4}, REPRODUCTION_SAMPLES)
    session.ask_ok(f'settings set Engine Tuning: {TAA}=on')
    try:
        jittered, _, _ = export(session, 'reproduction-taa', SCENE, REPRODUCTION_SAMPLES)
        resets = path_tracer_state(session)['resets']
        session.ask_ok(f'frames {SETTLE_FRAMES + 2}')
        kept_on = path_tracer_state(session)['resets'] == resets
    finally:
        session.ask_ok(f'settings set Engine Tuning: {TAA}=off')

    def describe(image):
        return 'same' if same_bits(first, image) else f'{count(np.any(first != image, axis=2))} pixels differ'

    check('[I9] the same Seed accumulates the same bits after returning from another Seed, with another Samples per Frame and with TAA',
          same_bits(first, returned) and same_bits(first, paced) and same_bits(first, jittered) and kept_on,
          f'returned: {describe(returned)}; Samples per Frame 4: {describe(paced)}; TAA: {describe(jittered)}, '
          f'{"no reset" if kept_on else "reset"} while TAA jitters')


def command_check(args):
    check = Checks()
    instance = start_instance('Debug', build_first=not args.no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            check_default_output(session, check)
            start_pose = default_scene_pose()
            session.ask_ok(f'pose {start_pose}')
            hits = front_face_hits(session, 'front-face-start')
            check_furnaces(session, check, hits)
            check_gltf_furnace(session, check, hits)
            check_direct_light_excluded(session, check, start_pose)
            check_reflections(session, check, start_pose)
            check_scene(session, check, start_pose)
            check_reproduction(session, check, start_pose)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('check ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def command_keep(args):
    instance = start_instance('Profile', build_first=not args.no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'pose {default_scene_pose()}')
            KEPT_DIR.mkdir(parents=True, exist_ok=True)
            for name, settings in KEEP_RUNS.items():
                _, _, path = export(session, f'start-{name}-{KEEP_SAMPLES}spp', KEEP | settings, KEEP_SAMPLES)
                kept = KEPT_DIR / path.name
                shutil.copy2(path, kept)
                exposure = PREVIEW_EXPOSURE / math.pi if settings['Output'] == IRRADIANCE['Output'] else PREVIEW_EXPOSURE
                make_view(kept, 'pt', kept.with_suffix('.png'), None, 1 << 16, exposure)
        finally:
            session.close()
    finally:
        stopped = stop_instance(instance)
    print(f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    print(f'kept: {KEPT_DIR}')
    return 0 if stopped['exit_reason'] == 'normal' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    for name, handler, help in [('check', command_check, 'judge the criteria and the D3D12 messages in Debug'),
                                ('keep', command_keep, 'keep the images for the presentation, in Profile')]:
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
