import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from measurementrecord import append_record_row, code_version, describe_machine
from viewer import REPO_ROOT, HarnessError, Session, baseline_scene, start_instance, stop_instance

RECORD = REPO_ROOT / 'docs' / 'emissive-sampling-measurements.md'
RECORD_SECTION = '## Emissive Samples per Probe 의 GPU time'

SCENE = baseline_scene('CornellBox.ini')
DEBUG_SECTION = 'Dynamic GI Debug'
VOLUME = 'Dynamic GI Volume'
BASE_DEBUG = {'Indirect': 'Probe', 'Pause Updates': 0, 'Show Probes': 0}
BASE_VOLUME = {'Probes X': 8, 'Probes Y': 8, 'Probes Z': 8, 'Rays per Probe': 256, 'Random Ray Rotation': 1}

EMISSIVE_SAMPLES_PER_PROBE = (0, 32, 64, 128, 256)
EMISSIVE_TRACE_SCOPE = 'Probe Emissive Trace'
SCOPES = ('Probe Trace', EMISSIVE_TRACE_SCOPE, 'Probes Update', 'Probe Lighting')

WARMUP_FRAMES = 16
PROFILE_FRAMES = 64
WAIT_TIMEOUT = 600


def assignments(settings):
    return '; '.join(f'{key}={value}' for key, value in settings.items())


def set_debug(session, settings):
    session.ask_ok(f'settings set {DEBUG_SECTION}: ' + assignments(settings))


def set_volume(session, settings):
    session.ask_ok(f'editor set {VOLUME}: ' + assignments(settings))


def gpu_ms(session, scope):
    return float(session.ask_ok(f'profile {scope} frames={PROFILE_FRAMES}', timeout=WAIT_TIMEOUT)['gpu_ms']['mean'])


def measure_samples(session, samples_per_probe):
    set_volume(session, {'Emissive Samples per Probe': samples_per_probe})
    session.ask_ok(f'frames {WARMUP_FRAMES}', timeout=WAIT_TIMEOUT)
    return {scope: 0.0 if scope == EMISSIVE_TRACE_SCOPE and samples_per_probe == 0 else gpu_ms(session, scope) for scope in SCOPES}


def format_row(today, code, machine, samples_per_probe, result):
    probes = 'x'.join(str(BASE_VOLUME[f'Probes {axis}']) for axis in 'XYZ')
    return [today, code, *machine, probes, str(BASE_VOLUME['Rays per Probe']), str(samples_per_probe), str(PROFILE_FRAMES),
            *(f'{result[scope]:.3f}' for scope in SCOPES)]


def measure(args):
    code = code_version()
    today = time.strftime('%Y-%m-%d')
    instance = start_instance('Profile', scene=SCENE, build_first=not args.no_build)
    rows = []
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            set_debug(session, BASE_DEBUG)
            set_volume(session, BASE_VOLUME)
            machine = describe_machine(session)
            print(', '.join(machine), flush=True)
            for samples_per_probe in EMISSIVE_SAMPLES_PER_PROBE:
                row = format_row(today, code, machine, samples_per_probe, measure_samples(session, samples_per_probe))
                print(' | '.join(row[5:]), flush=True)
                rows.append(row)
        finally:
            session.close()
    finally:
        stopped = stop_instance(instance)
    print(f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    if stopped['exit_reason'] != 'normal':
        return 1
    if args.record:
        for row in rows:
            append_record_row(RECORD, RECORD_SECTION, row)
        print(f'recorded in {RECORD}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=f'sweep Emissive Samples per Probe and measure the GPU time of the probe passes against {RECORD.name}')
    parser.add_argument('--no-build', action='store_true', help='use the exe as it is')
    parser.add_argument('--record', action='store_true', help=f'append the measured rows to {RECORD.name}')
    args = parser.parse_args()
    try:
        return measure(args)
    except HarnessError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
