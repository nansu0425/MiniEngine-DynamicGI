"""PathTracer 의 1 spp 의 비용을 잰다.

  python scripts/measure-path-tracer-cost.py            # Profile 로 띄워 1 spp 의 GPU time 을 잰다
  python scripts/measure-path-tracer-cost.py --record   # 잰 값을 RECORD 에 덧붙인다

app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. PT 의 Shading 은 measurement.BASE 의 Lambert 다. sky 는 늘 흰 Sky 의
Intensity 1 이다. SCENE 은 furnace 와 directional light 를 끈 Sponza 다.

SCENE 을 Max Bounces 64, Samples per Frame 1 로 쌓는 동안 Profiler 의 "Path Tracer" GPU time 을 COST_FRAMES 동안 읽는다.
Russian roulette 를 켠 것과 끈 것을 잰다.
"""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from measurement import BASE, RECORD, append_record_row, code_version, describe_machine, light_by_sky_only, set_path_tracer
from viewer import HarnessError, Session, start_instance, stop_instance

RECORD_SECTION = '## 작업 5: 1 spp 의 GPU time'

COST_WARMUP_SAMPLES = 16
COST_FRAMES = 256
# 가장 오래 기다리는 명령은 Russian roulette 를 끄고 COST_FRAMES 동안 재는 profile 로, 1 분쯤 걸린다.
WAIT_TIMEOUT = 900

SCENE = BASE


def measure(args):
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
            append_record_row(RECORD_SECTION, row)
        print(f'recorded in {RECORD}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true', help='use the exe as it is')
    parser.add_argument('--record', action='store_true', help=f'append the measured times to {RECORD.name}')
    args = parser.parse_args()
    try:
        return measure(args)
    except HarnessError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
