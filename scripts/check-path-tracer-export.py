"""PathTracer 의 누적값을 float image 로 export 해서 furnace 의 답, 같은 Seed 의 재현, NaN 과 Inf 인 sample 의 처리를 판정하고, shading
normal 이 잃는 energy 와 Russian roulette 의 pixel 단위 bias 를 잰다.

  python scripts/check-path-tracer-export.py check            # Debug 로 띄워 아래 기준을 모두 판정한다
  python scripts/check-path-tracer-export.py check --record   # 모두 통과하면 [N1], [R1] 에서 잰 값을 RECORD 에 덧붙인다

app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1 이다. 작업 5 의 확인은
check-lambert-path.py 가 따로 한다. PT 의 Shading 은 pathtracerchecks.BASE 의 Lambert 다. glTF BRDF 는 check-gltf-brdf.py 가 본다.

export 는 capture pt 가 쓰는 누적 buffer 의 DDS 다. rgb 는 평균, a 는 더한 sample 수다. 옆의 state 파일의 pt.samples 는 dispatch 한
sample 수이므로, 그 차가 NaN 이나 Inf 라서 더하지 않은 sample 수다. r 의 식과 뜻은 imagecompare.py 에 있다.

furnace 는 pathtracerchecks.py 의 WHITE_FURNACE 와 EMISSION_FURNACE 로 FURNACE_SAMPLES 만큼 쌓는다. Russian roulette 를 끄면 RNG 를 쓰는
양이 albedo, emission, sky 에 따라 달라지지 않으므로, Seed 와 Max Bounces 가 같은 furnace 들은 같은 path 를 그린다. 아래의 예측은
그래서 기대값이 아니라 sample 마다 성립한다. X_WF(B) 는 Max Bounces B 의 white furnace 로, B 번 반사하기 전에 escape 한 path 의 비율이다.
camera ray 가 바로 miss 한 path 도 escape 한 것으로 센다.

export:
  [E1] export 는 scene 크기다. NaN 과 Inf 를 만들지 않는 모든 run 에서 모든 pixel 의 a 가 pt.samples 와 같다.
  [E2] 장면을 Target 까지 쌓은 뒤 찍은 scene capture q 와 export v 가 모든 pixel, channel 에서 |q - v| <= 2^-m max(v, 2^-14) 다.
       scene color 는 R11G11B10_FLOAT 이고 m 은 mantissa 의 bit 수 (R, G 는 6, B 는 5), 2^-14 는 가장 작은 normal 값이다. 오른쪽은 v 가 든
       binade 의 1 ulp 이고 2^-14 아래에서는 denormal 의 ulp 라서, GPU 의 rounding mode 를 몰라도 성립한다. 이 GPU 가 denormal 을 0 으로
       flush 하지 않는 것은 작업 5 의 scene capture 에 2^-20 과 2^-19 격자의 값이 있는 것으로 봤다.

furnace:
  [F1] white furnace 를 Russian roulette off 와 on 으로 쌓으면 모든 pixel, channel 이 |X - 1| <= FURNACE_TOLERANCE 다.
  [F2] emission furnace 를 B in EMISSION_FURNACE_BOUNCES 로 쌓은 X 가 모든 pixel 에서 1 - 0.5^(B+1) (1 - X_WF(B)) 와 상대
       FURNACE_TOLERANCE 안에서 맞는다. B 번 반사하기 전에 escape 한 path 는 1, 못한 path 는 1 - 0.5^(B+1) 이다. emission 의 누적,
       albedo < 1 의 반사, B 의 off-by-one 을 본다.

shading normal: shading normal 둘레에서 고른 방향이 geometric normal 아래면 docs/policy.md 의 "camera 밖에서 본 doubleSided" 규칙대로 path 가
끝나서 energy 를 잃는다. DDGI 는 이 규칙을 모르므로 DDGI 와 PT 의 차에 반사마다 이 손실이 섞인다.
  [N1] ONE_REFLECTION_FURNACE 를 Geometric Normals 를 켜고 끈 두 run 으로 SHADING_NORMAL_SAMPLES 만큼 쌓는다. path 의 값은 반사가 잘리면
       1, 아니면 2 이므로, 두 run 의 차가 camera 에서 보이는 면에서 반사 한 번이 잃는 energy 의 비율이다. 모든 pixel 에서 그 차가
       [-FURNACE_TOLERANCE, 1 + FURNACE_TOLERANCE] 안이다. Geometric Normals 를 끈 white furnace 는 모든 pixel 이 1 + FURNACE_TOLERANCE
       이하다. 반사 한 번의 평균 손실, 손실이 있는 pixel 의 비율, 그 pixel 들의 평균 손실, white furnace 의 평균 손실을 찍는다. white
       furnace 는 path 가 escape 할 때까지 반사마다 손실이 쌓인 값이라 albedo 1 에서만 나오는 크기다.

Russian roulette:
  [R1] furnace 와 directional light 를 끈 Sponza 를 SCENE_SAMPLES 만큼 Russian roulette off (A) 와 on (B) 로, 각각 Seed 0 과 1 로 쌓은 네 export 의 r 이
       AGREEMENT_LIMIT 이하다. r^2 ~ 1 + bias^2 / noise^2 이므로 bias 의 rms 가 noise 의 0.14 배 아래라는 뜻이다. r, bias, noise 를 찍는다.
       Seed 0 과 1 이 독립이 아니면 분모가 작아져 r 이 커지므로 Seed 의 독립도 같이 본다.

재현:
  [S1] 장면을 Russian roulette on, Seed 0 으로 REPRODUCTION_SAMPLES 만큼 쌓은 export, Seed 1 로 바꿨다가 되돌려 다시 쌓은 export,
       Samples per Frame 4 로 다시 쌓은 export 가 rgba 의 bit 까지 같다. 뒤의 둘은 그 사이에 누적이 다시 시작된 것도 본다.
       sample 번호가 frame 과 무관하게 reset 뒤의 순번이므로 Samples per Frame 은 image 를 바꾸지 않는다.

NaN 과 Inf: Sky Light 의 Intensity 를 inf 로 두고 white furnace 를 Max Bounces INVALID_BOUNCES 로 쌓는다. escape 한 path 만 sky 를 본다.
  [V1] albedo 1 이면 escape 한 path 가 Inf 다.
  [V2] albedo 0 이면 escape 한 path 가 0 x Inf = NaN 이다.
  둘 다 모든 pixel 의 a 가 escape 하지 못한 path 수 FURNACE_SAMPLES x (1 - X_WF(INVALID_BOUNCES)) 와 같고 rgb 가 0 이다. 더한 sample 과
  버린 sample 이 섞인 pixel 이 있고, 버린 sample 이 0 보다 많다.

check 는 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다. [N1] 의 export 는
발표에 쓰도록 KEPT_DIR 에 DDS 와 보기용 PNG 로, 반사 한 번의 손실은 PNG 로 남긴다.
"""

import argparse
import json
import shutil
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from imagecompare import agreement
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import (BASE, EMISSION_FURNACE, FURNACE_SAMPLES, RECORD, light_by_sky_only, WHITE_FURNACE, append_record_row, code_version,
                              describe_machine, path_tracer_state, set_path_tracer, SKY_RADIANCE)
from viewer import HARNESS_DIR, PREVIEW_EXPOSURE, HarnessError, Session, capture, make_view, shots_dir, start_instance, stop_instance

KEPT_DIR = HARNESS_DIR / 'kept' / 'path-tracer-export'
SHADING_NORMAL_RECORD_SECTION = '## 작업 6: shading normal 이 잃는 energy'
RUSSIAN_ROULETTE_RECORD_SECTION = '## 작업 6: Russian roulette 의 pixel 단위 bias'

FURNACE_TOLERANCE = 1e-4
EMISSION_FURNACE_BOUNCES = [0, 1, 2, 4, 64]
# Sponza 에서 escape 한 path 와 못한 path 가 한 pixel 에 섞이는 반사 횟수
INVALID_BOUNCES = 4
# white furnace 가 Debug 에서 1 분쯤 걸린다.
SHADING_NORMAL_SAMPLES = 256
# 처음 맞은 곳의 emission 1 에, 반사가 잘리지 않으면 다음 hit 의 emission 이나 sky 1 을 더하고 끝난다.
ONE_REFLECTION_FURNACE = WHITE_FURNACE | {'Furnace Emission': 1, 'Max Bounces': 1}
# 반사 한 번의 손실을 보는 PNG 에서 흰색이 되는 손실
LOSS_VIEW_MAX = 0.5
SCENE = BASE
SCENE_SAMPLES = 256
# r 은 약 200 만 pixel 의 합이라 bias 가 없으면 1 근처에 모인다. imagecompare.py selftest 가 그 흔들림을 본다.
AGREEMENT_LIMIT = 1.01
REPRODUCTION_SAMPLES = 64
# [S1] 에서 Seed 1 로 쌓다가 Seed 0 으로 되돌리기 전에 기다리는 sample 수
SAMPLES_BEFORE_RETURN = 8

R11G11B10_MANTISSA_BITS = np.array([6, 6, 5])
R11G11B10_SMALLEST_NORMAL = 2.0 ** -14

# 가장 느린 run 은 Russian roulette 를 끈 장면의 SCENE_SAMPLES 로, Debug 에서 1 분쯤 걸린다.
WAIT_TIMEOUT = 900


def export(session, name):
    """capture pt 로 쓴 (rgba, 그 frame 의 pt.samples, DDS 경로)."""
    reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / name}')
    state = json.loads(Path(reply['state']).read_text(encoding='utf-8'))
    return read_dds_rgba32f(reply['path']), state['pt']['samples'], Path(reply['path'])


class Accumulations:
    """settings 로 처음부터 쌓아 export 하고, [E1] 을 판정하려고 NaN 과 Inf 를 만들지 않는 run 의 a 를 pt.samples 와 견준다."""

    def __init__(self, session):
        self.session = session
        self.counted = []

    def __call__(self, name, settings, samples, finite=True):
        set_path_tracer(self.session, settings | {'Target Samples': samples})
        self.session.ask_ok('pt reset')
        self.session.ask_ok(f'pt wait samples={samples}', timeout=WAIT_TIMEOUT)
        image, dispatched, path = export(self.session, name)
        if finite:
            self.counted.append((name, dispatched, int(np.count_nonzero(image[..., 3] != dispatched))))
        return image, path


def rgb64(image):
    return np.asarray(image[..., :3], dtype=np.float64)


def keep(path):
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    kept = KEPT_DIR / path.name
    shutil.copy2(path, kept)
    make_view(kept, 'pt', kept.with_suffix('.png'), None, 1 << 16, PREVIEW_EXPOSURE)


def check_furnaces(accumulate, check):
    """X_WF(INVALID_BOUNCES) 를 돌려준다."""
    for russian_roulette in (0, 1):
        image, _ = accumulate(f'white-furnace-rr{russian_roulette}', WHITE_FURNACE | {'Russian Roulette': russian_roulette}, FURNACE_SAMPLES)
        deviation = float(np.max(np.abs(rgb64(image) - 1.0)))
        check(f'[F1] white furnace with Russian roulette {"on" if russian_roulette else "off"} is within {FURNACE_TOLERANCE:g} of 1',
              deviation <= FURNACE_TOLERANCE, f'max |X - 1| {deviation:.3g}')

    escaped, deviations = {}, {}
    for bounces in EMISSION_FURNACE_BOUNCES:
        white, _ = accumulate(f'white-furnace-b{bounces}', WHITE_FURNACE | {'Max Bounces': bounces}, FURNACE_SAMPLES)
        emission, _ = accumulate(f'emission-furnace-b{bounces}', EMISSION_FURNACE | {'Max Bounces': bounces}, FURNACE_SAMPLES)
        escaped[bounces] = rgb64(white)
        predicted = 1.0 - 0.5 ** (bounces + 1) * (1.0 - escaped[bounces])
        deviations[bounces] = float(np.max(np.abs(rgb64(emission) / predicted - 1.0)))
    check(f'[F2] emission furnace matches 1 - 0.5^(B+1) (1 - X_WF(B)) within {FURNACE_TOLERANCE:g} for B in {EMISSION_FURNACE_BOUNCES}',
          all(deviation <= FURNACE_TOLERANCE for deviation in deviations.values()),
          ', '.join(f'B {bounces}: max relative {deviation:.3g}, mean X_WF {float(escaped[bounces].mean()):.4g}'
                    for bounces, deviation in deviations.items()))
    return escaped[INVALID_BOUNCES]


def check_invalid_samples(accumulate, check, escaped):
    expected = np.rint(FURNACE_SAMPLES * (1.0 - escaped[..., 0]))
    for name, albedo, kind in (('[V1]', 1, 'Inf'), ('[V2]', 0, 'NaN')):
        settings = WHITE_FURNACE | {'Max Bounces': INVALID_BOUNCES, SKY_RADIANCE: 'inf', 'Furnace Albedo': albedo}
        image, _ = accumulate(f'invalid-{kind.lower()}', settings, FURNACE_SAMPLES, finite=False)
        added = image[..., 3]
        mismatched = int(np.count_nonzero(added != expected))
        nonzero = int(np.count_nonzero(np.any(image[..., :3] != 0.0, axis=2)))
        mixed = int(np.count_nonzero((added > 0) & (added < FURNACE_SAMPLES)))
        dropped = int(np.sum(FURNACE_SAMPLES - added, dtype=np.int64))
        check(f'{name} with albedo {albedo} and an infinite sky, paths that escape are {kind} and are counted but not added',
              mismatched == 0 and nonzero == 0 and mixed > 0 and dropped > 0,
              f'{mismatched} pixels whose a is not the paths that stayed, {nonzero} pixels whose rgb is not 0, '
              f'{mixed} pixels with both added and dropped samples, {dropped} samples dropped')


def keep_loss_map(loss, name):
    KEPT_DIR.mkdir(parents=True, exist_ok=True)
    gray = (np.clip(loss / LOSS_VIEW_MAX, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(gray).save(KEPT_DIR / f'{name}.png')


def check_shading_normals(accumulate, check):
    """측정 기록의 (반사 한 번의 평균 손실, 손실이 있는 pixel 의 비율, 그 pixel 들의 평균 손실, white furnace 의 평균 손실) 을 돌려준다."""
    geometric, _ = accumulate('one-reflection-geometric-normals', ONE_REFLECTION_FURNACE, SHADING_NORMAL_SAMPLES)
    shading, _ = accumulate('one-reflection-shading-normals', ONE_REFLECTION_FURNACE | {'Geometric Normals': 0}, SHADING_NORMAL_SAMPLES)
    loss = (rgb64(geometric) - rgb64(shading)).mean(axis=2)
    keep_loss_map(loss, 'one-reflection-loss')
    lossy = loss > 0.0
    check('[N1] one reflection about the shading normal loses between 0 and 1 of the energy in every pixel',
          loss.min() >= -FURNACE_TOLERANCE and loss.max() <= 1.0 + FURNACE_TOLERANCE,
          f'min {loss.min():.4g}, max {loss.max():.4g}; mean {loss.mean():.4g}, {lossy.mean():.2%} of pixels lose energy, '
          f'by {loss[lossy].mean() if lossy.any() else 0.0:.4g} on average')

    white, path = accumulate('white-furnace-shading-normals', WHITE_FURNACE | {'Geometric Normals': 0}, SHADING_NORMAL_SAMPLES)
    keep(path)
    rgb = rgb64(white)
    check(f'[N1] white furnace with shading normals is {1 + FURNACE_TOLERANCE:g} or less everywhere', rgb.max() <= 1.0 + FURNACE_TOLERANCE,
          f'max {rgb.max():.9g}, mean loss {1.0 - rgb.mean():.4g}')
    return (float(loss.mean()), float(lossy.mean()), float(loss[lossy].mean()) if lossy.any() else 0.0, float(1.0 - rgb.mean()))


def check_scene_capture(session, check, exported):
    q = rgb64(read_dds_rgba32f(capture(session, 'scene', 'scene-after-export')))
    v = rgb64(exported)
    tolerance = np.ldexp(1.0, -R11G11B10_MANTISSA_BITS) * np.maximum(v, R11G11B10_SMALLEST_NORMAL)
    error = np.abs(q - v)
    outside = int(np.count_nonzero(error > tolerance))
    denormal = int(np.count_nonzero((v > 0.0) & (v < R11G11B10_SMALLEST_NORMAL)))
    check('[E2] the scene capture is the export within one R11G11B10 ulp per channel', outside == 0,
          f'{outside} values outside, largest error {float(np.max(error / tolerance)):.3g} ulp, {denormal} values below 2^-14')


def check_russian_roulette(session, accumulate, check):
    """측정 기록의 (r, bias, noise) 를 돌려준다. bias 와 noise 는 Russian roulette 를 끈 Seed 0 의 rms 에 대한 비율이다."""
    images = {}
    for seed in (0, 1):
        for russian_roulette in (0, 1):
            images[russian_roulette, seed], _ = accumulate(f'scene-rr{russian_roulette}-seed{seed}',
                                                           SCENE | {'Seed': seed, 'Russian Roulette': russian_roulette}, SCENE_SAMPLES)
            if (russian_roulette, seed) == (1, 0):
                check_scene_capture(session, check, images[1, 0])
    result = agreement(images[0, 0], images[0, 1], images[1, 0], images[1, 1])
    scale = float(np.sqrt(np.mean(np.square(rgb64(images[0, 0])))))
    measured = (result.ratio, result.bias / scale, result.noise / scale)
    check(f'[R1] r of the scene with Russian roulette off and on is {AGREEMENT_LIMIT:g} or less', result.ratio <= AGREEMENT_LIMIT,
          f'r {result.ratio:.5g}, bias rms {measured[1]:.3g} and noise rms {measured[2]:.3g} of the image rms {scale:.4g}')
    return measured


def check_reproduction(session, accumulate, check):
    settings = SCENE | {'Seed': 0, 'Russian Roulette': 1}
    first, _ = accumulate('reproduction-first', settings, REPRODUCTION_SAMPLES)
    before = path_tracer_state(session)['resets']

    set_path_tracer(session, {'Seed': 1})
    session.ask_ok(f'pt wait samples={SAMPLES_BEFORE_RETURN}', timeout=WAIT_TIMEOUT)
    set_path_tracer(session, {'Seed': 0})
    session.ask_ok('frames 2')
    session.ask_ok(f'pt wait samples={REPRODUCTION_SAMPLES}', timeout=WAIT_TIMEOUT)
    returned, dispatched, _ = export(session, 'reproduction-returned')
    accumulate.counted.append(('reproduction-returned', dispatched, int(np.count_nonzero(returned[..., 3] != dispatched))))
    after_return = path_tracer_state(session)['resets']

    paced, _ = accumulate('reproduction-paced', settings | {'Samples per Frame': 4}, REPRODUCTION_SAMPLES)
    after_pace = path_tracer_state(session)['resets']

    def same_bits(image):
        return bool(np.array_equal(first.view(np.uint32), image.view(np.uint32)))

    def describe(image):
        return 'same' if same_bits(image) else f'{int(np.count_nonzero(np.any(first != image, axis=2)))} pixels differ'

    check('[S1] the same Seed accumulates the same bits after returning from another Seed and with another Samples per Frame',
          same_bits(returned) and same_bits(paced) and after_return == before + 2 and after_pace > after_return,
          f'returned: {describe(returned)}, resets {before} -> {after_return}; Samples per Frame 4: {describe(paced)}, resets -> {after_pace}')


def check_counts(session, accumulate, check, image_shape):
    scene = session.ask_ok('state ui')['state']['ui']['scene']
    check('[E1] the export is the size of the scene', image_shape[:2] == (scene['height'], scene['width']),
          f'export {image_shape[1]}x{image_shape[0]}, scene {scene["width"]}x{scene["height"]}')
    wrong = [(name, dispatched, pixels) for name, dispatched, pixels in accumulate.counted if pixels]
    check('[E1] every export without NaN or Inf has a = pt.samples in every pixel', bool(accumulate.counted) and not wrong,
          f'{len(accumulate.counted)} exports; ' + (', '.join(f'{name}: {pixels} pixels differ from {dispatched}' for name, dispatched, pixels in wrong)
                                                    or 'all match'))


def command_check(args):
    check = Checks()
    code = code_version()
    instance = start_instance('Debug', build_first=not args.no_build)
    measured = None
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            light_by_sky_only(session)
            machine = describe_machine(session)
            accumulate = Accumulations(session)
            escaped = check_furnaces(accumulate, check)
            check_invalid_samples(accumulate, check, escaped)
            shading_normals = check_shading_normals(accumulate, check)
            russian_roulette = check_russian_roulette(session, accumulate, check)
            check_reproduction(session, accumulate, check)
            check_counts(session, accumulate, check, escaped.shape)
            measured = (machine, shading_normals, russian_roulette)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('check ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
    print(f'kept exports: {KEPT_DIR}')
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    if check.failures:
        return 1
    if args.record:
        record(code, *measured)
        print(f'recorded in {RECORD}')
    return 0


def record(code, machine, shading_normals, russian_roulette):
    date = time.strftime('%Y-%m-%d')
    append_record_row(SHADING_NORMAL_RECORD_SECTION, [date, code, *machine, str(SHADING_NORMAL_SAMPLES)] +
                      [f'{value:.4g}' for value in shading_normals])
    append_record_row(RUSSIAN_ROULETTE_RECORD_SECTION, [date, code, *machine, str(SCENE['Max Bounces']), str(SCENE_SAMPLES)] +
                      [f'{russian_roulette[0]:.5g}', f'{russian_roulette[1]:.3g}', f'{russian_roulette[2]:.3g}'])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('check', help='judge the criteria and the D3D12 messages in Debug')
    command.add_argument('--no-build', action='store_true', help='use the exe as it is')
    command.add_argument('--record', action='store_true', help=f'append what [N1] and [R1] measured to {RECORD.name} when all pass')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (HarnessError, ImageFileError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
