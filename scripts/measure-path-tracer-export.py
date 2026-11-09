"""PathTracer 의 누적값을 float image 로 export 해서 shading normal 이 잃는 energy 와 Russian roulette 의 bias 를 잰다.

  python scripts/measure-path-tracer-export.py            # Debug 로 띄워 아래 값을 잰다
  python scripts/measure-path-tracer-export.py --record   # 잰 값을 RECORD 에 덧붙인다

app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. PT 의 Shading 은 measurement.BASE 의 Lambert 다.

export 는 capture pt 가 쓰는 누적 buffer 의 DDS 다. rgb 는 평균, a 는 더한 sample 수다. r 의 식과 뜻은 imagecompare.py 에 있다.

shading normal: shading normal 둘레에서 고른 방향이 geometric normal 아래면 docs/policy.md 의 "camera 밖에서 본 doubleSided" 규칙대로 path 가
끝나서 energy 를 잃는다. DDGI 는 이 규칙을 모르므로 DDGI 와 PT 의 차에 반사마다 이 손실이 섞인다.
  ONE_REFLECTION_FURNACE 를 Geometric Normals 를 켜고 끈 두 run 으로 SHADING_NORMAL_SAMPLES 만큼 쌓는다. path 의 값은 반사가 잘리면
  1, 아니면 2 이므로, 두 run 의 차가 camera 에서 보이는 면에서 반사 한 번이 잃는 energy 의 비율이다. Geometric Normals 를 끈 white
  furnace 를 SHADING_NORMAL_SAMPLES 만큼 쌓는다. 반사 한 번의 평균 손실, 손실이 있는 pixel 의 비율, 그 pixel 들의 평균 손실, white
  furnace 의 평균 손실을 찍는다. white furnace 는 path 가 escape 할 때까지 반사마다 손실이 쌓인 값이라 albedo 1 에서만 나오는 크기다.

Russian roulette: SCENE 은 furnace 와 directional light 를 끈 Sponza 다. SCENE 을 SCENE_SAMPLES 만큼 Russian roulette off (A) 와 on (B)
로, 각각 Seed 0 과 1 로 쌓는다.
  네 export 의 r, bias, noise 를 찍는다. r^2 ~ 1 + bias^2 / noise^2 이다.
"""

import argparse
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from imagecompare import agreement
from imagefiles import ImageFileError, read_dds_rgba32f
from measurement import (BASE, RECORD, WHITE_FURNACE, append_record_row, code_version, describe_machine, light_by_sky_only,
                         set_path_tracer)
from viewer import HarnessError, Session, shots_dir, start_instance, stop_instance

SHADING_NORMAL_RECORD_SECTION = '## 작업 6: shading normal 이 잃는 energy'
RUSSIAN_ROULETTE_RECORD_SECTION = '## 작업 6: Russian roulette 의 pixel 단위 bias'

# SHADING_NORMAL_SAMPLES 로 쌓는 white furnace 는 Debug 에서 1 분쯤 걸린다.
SHADING_NORMAL_SAMPLES = 256
# 처음 맞은 곳의 emission 1 에, 반사가 잘리지 않으면 다음 hit 의 emission 이나 sky 1 을 더하고 끝난다.
ONE_REFLECTION_FURNACE = WHITE_FURNACE | {'Furnace Emission': 1, 'Max Bounces': 1}
SCENE = BASE
SCENE_SAMPLES = 256

# 가장 느린 run 은 Russian roulette 를 끈 SCENE 의 SCENE_SAMPLES 로, Debug 에서 1 분쯤 걸린다.
WAIT_TIMEOUT = 900


def accumulate(session, name, settings, samples):
    """settings 로 처음부터 samples 만큼 쌓은 export 의 rgba."""
    set_path_tracer(session, settings | {'Target Samples': samples})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={samples}', timeout=WAIT_TIMEOUT)
    reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / name}')
    return read_dds_rgba32f(reply['path'])


def rgb64(image):
    return np.asarray(image[..., :3], dtype=np.float64)


def measure_shading_normals(session):
    """측정 기록의 (반사 한 번의 평균 손실, 손실이 있는 pixel 의 비율, 그 pixel 들의 평균 손실, white furnace 의 평균 손실) 을 돌려준다."""
    geometric = accumulate(session, 'one-reflection-geometric-normals', ONE_REFLECTION_FURNACE, SHADING_NORMAL_SAMPLES)
    shading = accumulate(session, 'one-reflection-shading-normals', ONE_REFLECTION_FURNACE | {'Geometric Normals': 0}, SHADING_NORMAL_SAMPLES)
    loss = (rgb64(geometric) - rgb64(shading)).mean(axis=2)
    lossy = loss > 0.0
    print(f'one reflection about the shading normal: loss min {loss.min():.4g}, max {loss.max():.4g}; mean {loss.mean():.4g}, '
          f'{lossy.mean():.2%} of pixels lose energy, by {loss[lossy].mean() if lossy.any() else 0.0:.4g} on average', flush=True)

    white = rgb64(accumulate(session, 'white-furnace-shading-normals', WHITE_FURNACE | {'Geometric Normals': 0}, SHADING_NORMAL_SAMPLES))
    print(f'white furnace with shading normals: max {white.max():.9g}, mean loss {1.0 - white.mean():.4g} at {SHADING_NORMAL_SAMPLES} spp',
          flush=True)
    return (float(loss.mean()), float(lossy.mean()), float(loss[lossy].mean()) if lossy.any() else 0.0, float(1.0 - white.mean()))


def measure_russian_roulette(session):
    """측정 기록의 (r, bias, noise) 를 돌려준다. bias 와 noise 는 Russian roulette 를 끈 Seed 0 의 rms 에 대한 비율이다."""
    images = {(russian_roulette, seed): accumulate(session, f'scene-rr{russian_roulette}-seed{seed}',
                                                   SCENE | {'Seed': seed, 'Russian Roulette': russian_roulette}, SCENE_SAMPLES)
              for seed in (0, 1) for russian_roulette in (0, 1)}
    result = agreement(images[0, 0], images[0, 1], images[1, 0], images[1, 1])
    scale = float(np.sqrt(np.mean(np.square(rgb64(images[0, 0])))))
    measured = (result.ratio, result.bias / scale, result.noise / scale)
    print(f'scene with Russian roulette off and on: r {result.ratio:.5g}, bias rms {measured[1]:.3g} and noise rms {measured[2]:.3g} '
          f'of the image rms {scale:.4g}', flush=True)
    return measured


def measure(args):
    code = code_version()
    instance = start_instance('Debug', build_first=not args.no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            light_by_sky_only(session)
            machine = describe_machine(session)
            shading_normals = measure_shading_normals(session)
            russian_roulette = measure_russian_roulette(session)
        finally:
            session.close()
    finally:
        stopped = stop_instance(instance)
    print(f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    if stopped['exit_reason'] != 'normal':
        return 1
    if args.record:
        record(code, machine, shading_normals, russian_roulette)
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
    parser.add_argument('--no-build', action='store_true', help='use the exe as it is')
    parser.add_argument('--record', action='store_true', help=f'append the measured values to {RECORD.name}')
    args = parser.parse_args()
    try:
        return measure(args)
    except (HarnessError, ImageFileError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
