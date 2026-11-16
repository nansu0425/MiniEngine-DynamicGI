import argparse
import itertools
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from imagefiles import read_dds_r32f, read_dds_rg32f
from measurementrecord import append_record_row, code_version, describe_machine
from viewer import REPO_ROOT, HarnessError, Session, shots_dir, start_instance, stop_instance

RECORD = REPO_ROOT / 'docs' / 'probe-visibility-measurements.md'
RECORD_SECTION = '## Distance Sharpness, distance 해상도, ray 수'
PLOT_SCRIPT = Path(__file__).resolve().parent / 'plot-probe-distance.py'

DEBUG_SECTION = 'Engine Tuning'
DEBUG_KEY_PREFIX = 'Dynamic GI/'
VOLUME = 'Dynamic GI Volume'
BASE_DEBUG = {'Indirect': 'Probe', 'Pause Updates': 0, 'Show Probes': 0}
BASE_VOLUME = {'Visibility': 'Chebyshev', 'Hysteresis': 0.95, 'Probes X': 16, 'Probes Y': 8, 'Probes Z': 8}

DISTANCE_TEXELS_PER_SIDE = (8, 16)
RAYS_PER_PROBE = (64, 128, 256)
DISTANCE_SHARPNESS = (1, 10, 20, 50)

CONVERGENCE_UPDATES = 300
PROFILE_FRAMES = 64
OVERSHOOT_RATIO = 1.1
WAIT_TIMEOUT = 600


def assignments(settings, key_prefix=''):
    return '; '.join(f'{key_prefix}{key}={value}' for key, value in settings.items())


def set_debug(session, settings):
    session.ask_ok(f'settings set {DEBUG_SECTION}: ' + assignments(settings, DEBUG_KEY_PREFIX))


def set_volume(session, settings):
    session.ask_ok(f'editor set {VOLUME}: ' + assignments(settings))


def probe_updates(session):
    return session.ask_ok('state gi')['state']['gi']['probe']['updates']


def wait_for_updates(session, updates):
    target = probe_updates(session) + updates
    while (remaining := target - probe_updates(session)) > 0:
        session.ask_ok(f'frames {remaining}', timeout=WAIT_TIMEOUT)


def save_atlas(session, texture, path):
    session.ask_ok(f'texture-viewer select {texture}')
    return session.ask_ok(f'texture-viewer save {path}')['path']


def tile_interior(atlas, texels_per_side):
    tile = texels_per_side + 2
    rows, columns = atlas.shape[0] // tile, atlas.shape[1] // tile
    tiles = atlas.reshape(rows, tile, columns, tile, *atlas.shape[2:])
    return tiles[:, 1:-1, :, 1:-1].reshape(-1, *atlas.shape[2:])


def measure_combination(session, texels_per_side, rays_per_probe, sharpness, folder):
    name = f'n{texels_per_side}-r{rays_per_probe}-s{sharpness}'
    set_volume(session, {'Distance Texels per Side': texels_per_side, 'Rays per Probe': rays_per_probe, 'Distance Sharpness': sharpness})
    wait_for_updates(session, CONVERGENCE_UPDATES)

    trace_ms = float(session.ask_ok(f'profile Probe Trace frames={PROFILE_FRAMES}', timeout=WAIT_TIMEOUT)['gpu_ms']['mean'])
    update_ms = float(session.ask_ok(f'profile Probes Update frames={PROFILE_FRAMES}', timeout=WAIT_TIMEOUT)['gpu_ms']['mean'])

    set_debug(session, {'Pause Updates': 1})
    truth = tile_interior(read_dds_r32f(save_atlas(session, 'Probes Texel Distance', folder / f'{name}-texel')), texels_per_side)
    before = tile_interior(read_dds_rg32f(save_atlas(session, 'Probes Distance', folder / f'{name}-before')), texels_per_side)[:, 0]
    session.ask_ok('gi probe step')
    after = tile_interior(read_dds_rg32f(save_atlas(session, 'Probes Distance', folder / f'{name}-after')), texels_per_side)[:, 0]
    set_debug(session, {'Pause Updates': 0})

    ratio = before / truth
    change = np.abs(after - before) / truth
    p50, p90, p99 = np.percentile(ratio, [50, 90, 99])
    return {'p50': p50, 'p90': p90, 'p99': p99, 'overshoot': float((ratio > OVERSHOOT_RATIO).mean()),
            'change_p90': float(np.percentile(change, 90)), 'trace_ms': trace_ms, 'update_ms': update_ms}


def format_row(today, code, machine, texels_per_side, rays_per_probe, sharpness, result):
    return [today, code, *machine, str(texels_per_side), str(rays_per_probe), str(sharpness),
            f'{result["p50"]:.3f}', f'{result["p90"]:.3f}', f'{result["p99"]:.3f}', f'{result["overshoot"]:.3f}',
            f'{result["change_p90"]:.4f}', f'{result["trace_ms"]:.3f}', f'{result["update_ms"]:.3f}']


def measure(args):
    code = code_version()
    today = time.strftime('%Y-%m-%d')
    instance = start_instance('Profile', build_first=not args.no_build)
    rows = []
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            set_debug(session, BASE_DEBUG)
            set_volume(session, BASE_VOLUME)
            machine = describe_machine(session)
            folder = shots_dir(instance)
            print(', '.join(machine), flush=True)
            for texels_per_side, rays_per_probe, sharpness in itertools.product(DISTANCE_TEXELS_PER_SIDE, RAYS_PER_PROBE, DISTANCE_SHARPNESS):
                result = measure_combination(session, texels_per_side, rays_per_probe, sharpness, folder)
                row = format_row(today, code, machine, texels_per_side, rays_per_probe, sharpness, result)
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
        subprocess.run([sys.executable, str(PLOT_SCRIPT)], check=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=f'sweep the probe distance settings and measure the distance atlas against {RECORD.name}')
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
