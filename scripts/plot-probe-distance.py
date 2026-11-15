import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, NullLocator, ScalarFormatter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer import REPO_ROOT, HarnessError

RECORD = REPO_ROOT / 'docs' / 'probe-visibility-measurements.md'
RECORD_SECTION = '## Distance Sharpness, distance 해상도, ray 수'
IMAGE = REPO_ROOT / 'docs' / 'images' / 'probe-distance-sweep.png'

RAY_COLORS = {64: '#2a78d6', 128: '#eb6834', 256: '#1baf7a'}
RAY_MARKERS = {64: 'o', 128: '^', 256: 's'}
TEXEL_LINE_STYLES = {8: '-', 16: '--'}
GRID_COLOR = '#e1e0d9'
MUTED_COLOR = '#898781'
TEXT_COLOR = '#52514e'

METRICS = [
    ('p90', 'p90 비 (mean / 진짜 거리)', False),
    ('p99', 'p99 비 (mean / 진짜 거리, log)', True),
    ('1.1 배 초과', '1.1 배 초과 texel 비율', False),
    ('변화 p90', '변화 p90 (update 한 번의 noise)', False),
]


def read_record_rows():
    lines = RECORD.read_text(encoding='utf-8').splitlines()
    try:
        start = lines.index(RECORD_SECTION)
    except ValueError:
        raise HarnessError(f'{RECORD} has no section "{RECORD_SECTION}"')
    end = next((index for index in range(start + 1, len(lines)) if lines[index].startswith('## ')), len(lines))
    table = [line for line in lines[start + 1:end] if line.startswith('|')]
    header = [cell.strip() for cell in table[0].strip('|').split('|')]
    return [dict(zip(header, (cell.strip() for cell in line.strip('|').split('|')))) for line in table[2:]]


def latest_by_combination(rows):
    latest = {}
    for row in rows:
        latest[(int(row['N']), int(row['rays']), float(row['sharpness']))] = row
    return latest


def style_axes(axes, title):
    axes.set_title(title, fontsize=11, color=TEXT_COLOR, loc='left')
    axes.grid(axis='y', color=GRID_COLOR, linewidth=1)
    axes.set_axisbelow(True)
    for side in ('top', 'right'):
        axes.spines[side].set_visible(False)
    for side in ('left', 'bottom'):
        axes.spines[side].set_color('#c3c2b7')
    axes.tick_params(colors=MUTED_COLOR, labelsize=9)


def plot_metrics(figure_axes, latest):
    texels = sorted({key[0] for key in latest})
    rays = sorted({key[1] for key in latest})
    sharpnesses = sorted({key[2] for key in latest})
    positions = range(len(sharpnesses))
    for axes, (column, title, log) in zip(figure_axes, METRICS):
        for texels_per_side in texels:
            for rays_per_probe in rays:
                values = [float(latest[(texels_per_side, rays_per_probe, sharpness)][column]) for sharpness in sharpnesses]
                axes.plot(positions, values, color=RAY_COLORS[rays_per_probe], marker=RAY_MARKERS[rays_per_probe], markersize=5,
                          linestyle=TEXEL_LINE_STYLES[texels_per_side], linewidth=2, label=f'N {texels_per_side}, rays {rays_per_probe}')
        axes.set_xticks(list(positions), [f'{sharpness:g}' for sharpness in sharpnesses])
        axes.set_xlabel('Distance Sharpness', fontsize=9, color=MUTED_COLOR)
        if log:
            axes.set_yscale('log')
            axes.yaxis.set_major_locator(FixedLocator([1, 2, 5, 10, 20, 50]))
            axes.yaxis.set_major_formatter(ScalarFormatter())
            axes.yaxis.set_minor_locator(NullLocator())
        style_axes(axes, title)


def plot_cost(axes, latest):
    combinations = sorted({(key[0], key[1]) for key in latest})
    trace, update = [], []
    for texels_per_side, rays_per_probe in combinations:
        rows = [row for key, row in latest.items() if key[:2] == (texels_per_side, rays_per_probe)]
        trace.append(sum(float(row['Probe Trace (ms)']) for row in rows) / len(rows))
        update.append(sum(float(row['Probes Update (ms)']) for row in rows) / len(rows))
    positions = range(len(combinations))
    colors = [RAY_COLORS[rays_per_probe] for _, rays_per_probe in combinations]
    axes.bar(positions, trace, width=0.5, color=colors, edgecolor='white', linewidth=1.5, label='Probe Trace')
    axes.bar(positions, update, width=0.5, bottom=trace, color=colors, alpha=0.4, edgecolor='white', linewidth=1.5, label='Probes Update')
    for position, total in zip(positions, (a + b for a, b in zip(trace, update))):
        axes.text(position, total, f'{total:.2f}', ha='center', va='bottom', fontsize=9, color=TEXT_COLOR)
    axes.set_xticks(list(positions), [f'N {n}\nrays {r}' for n, r in combinations])
    axes.set_ylabel('ms', fontsize=9, color=MUTED_COLOR)
    style_axes(axes, 'GPU time (진한 부분 Probe Trace, 연한 부분 Probes Update, sharpness 별 평균)')


def plot(image):
    plt.rcParams['font.family'] = ['Malgun Gothic', 'DejaVu Sans']
    plt.rcParams['axes.unicode_minus'] = False
    rows = read_record_rows()
    if not rows:
        raise HarnessError(f'the section "{RECORD_SECTION}" of {RECORD} has no rows')
    latest = latest_by_combination(rows)
    newest = rows[-1]

    figure = plt.figure(figsize=(15, 9), dpi=110)
    grid = figure.add_gridspec(2, 3, hspace=0.45, wspace=0.25)
    metric_axes = [figure.add_subplot(grid[0, 0]), figure.add_subplot(grid[0, 1]), figure.add_subplot(grid[0, 2]),
                   figure.add_subplot(grid[1, 0])]
    plot_metrics(metric_axes, latest)
    plot_cost(figure.add_subplot(grid[1, 1:]), latest)

    handles, labels = metric_axes[0].get_legend_handles_labels()
    figure.legend(handles, labels, loc='upper center', ncol=6, frameon=False, fontsize=9, handlelength=4, bbox_to_anchor=(0.5, 0.965))
    figure.suptitle(f'코드 {newest["코드"]} · {newest["config"]} · {newest["GPU"]} · {newest["scene"]}', fontsize=10, color=MUTED_COLOR, y=0.995)
    image.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(image, bbox_inches='tight', facecolor='white')
    plt.close(figure)
    return image


def main():
    parser = argparse.ArgumentParser(description=f'draw the rows of {RECORD.name} as {IMAGE.name}')
    parser.parse_args()
    try:
        print(f'wrote {plot(IMAGE)}')
        return 0
    except HarnessError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
