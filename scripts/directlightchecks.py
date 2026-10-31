"""Sponza 에서 PT 의 directional light 직접광이 Light Visibility 를 따르고 반사가 더하는 빛과 어긋나지 않는지 보는 판정 ([P6], [P7]).

check-directional-light.py 가 Shading Lambert 로, check-gltf-brdf.py 가 Shading glTF 로 같은 판정을 하도록 여기에 모은다. 기준의
뜻과 임계값의 근거는 check-directional-light.py 의 docstring 에 있다.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from imagefiles import read_dds_rgba32f
from imagemasks import eroded
from pathtracerchecks import BASE, SKY_RADIANCE, set_path_tracer
from viewer import SETTLE_FRAMES, capture

LIGHT_OBJECT = 'Directional Light'
EDGE_PIXELS = 2
# low 는 harness baseline 의 시점에 보이는 곳과 가려진 곳이 둘 다 있는 낮은 방향이다. 고도 20 에서는 모든 방위에서 보이는 곳이 없었다.
SPONZA_LIGHTS = {'default': None, 'low': 'elevation=30 azimuth=180'}
PT_SAMPLES = 4
MIN_REGION_PIXELS = 1000
# 작업 7 을 판정할 때 기본 방향에서 가려졌는데 밝은 pixel 은 3.4e-5, 보이는데 어두운 pixel 은 1.9e-5 였다.
MISMATCH_FRACTION = 1e-4
MAX_MISMATCH_CLUSTER = 4
BOUNCE_STEPS = [1, 2, 4]
WAIT_TIMEOUT = 900
DIRECT = BASE | {SKY_RADIANCE: 0, 'Max Bounces': 1, 'Russian Roulette': 0, 'Target Samples': PT_SAMPLES}


def overlay(session, view, name):
    session.ask_ok(f'settings set Engine Tuning: Ray Tracing/View={view}')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    image = np.asarray(Image.open(capture(session, 'overlay', name)).convert('RGB'))
    session.ask_ok('settings set Engine Tuning: Ray Tracing/View=Off')
    return image


def accumulate_export(session, settings, name):
    """settings 로 처음부터 쌓은 PT export 의 rgba, NaN 이나 Inf 라서 버린 sample 수, export 의 경로."""
    set_path_tracer(session, settings)
    session.ask_ok('pt reset')
    samples = session.ask_ok(f'pt wait samples={settings["Target Samples"]}', timeout=WAIT_TIMEOUT)['samples']
    path = capture(session, 'pt', name)
    rgba = read_dds_rgba32f(path)
    return rgba, int(np.sum(samples - rgba[..., 3])), path


def set_light(session, values):
    """editor get 이 주는 key 와 value (scene file 의 Directional Light section) 로 light 를 정한다."""
    session.ask_ok(f'editor set {LIGHT_OBJECT}: ' + '; '.join(f'{key}={value}' for key, value in values.items()))


def largest_cluster(mask):
    """mask 에서 4 방향으로 이어진 가장 큰 덩어리의 pixel 수"""
    left = {(int(y), int(x)) for y, x in zip(*np.nonzero(mask))}
    largest = 0
    while left:
        stack, size = [left.pop()], 0
        while stack:
            y, x = stack.pop()
            size += 1
            for neighbor in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if neighbor in left:
                    left.remove(neighbor)
                    stack.append(neighbor)
        largest = max(largest, size)
    return largest


def check_sponza_direct(session, check, start_pose, default_light, label, settings, reflects_nothing, keep):
    """[P6]. settings 는 DIRECT 에 더할 PT 설정이고, reflects_nothing(base_color, metallic) 은 Base Color 와 Metallic view 의 rgb 에서
    그 Shading 의 f 가 0 이라 보이는데 어두워도 되는 pixel 의 mask 다. keep 은 쌓은 export 의 경로를 받는다."""
    for name, request in SPONZA_LIGHTS.items():
        session.ask_ok(f'pose {start_pose}')
        set_light(session, default_light)
        if request:
            session.ask_ok(f'light set {request}')
        visibility = overlay(session, 'Debug: Light Visibility', f'sponza-{name}-visibility')
        base_color = overlay(session, 'Debug: Base Color', f'sponza-{name}-base-color')
        metallic = overlay(session, 'Debug: Metallic', f'sponza-{name}-metallic')
        rgba, invalid, path = accumulate_export(session, DIRECT | {'Geometric Normals': 1} | settings, f'sponza-{name}-direct')
        keep(path)
        dark = np.all(rgba[..., :3] == 0.0, axis=2)
        occluded = eroded(np.all(visibility == 0, axis=2), EDGE_PIXELS)
        visible = eroded(np.all(visibility == 255, axis=2), EDGE_PIXELS)
        mismatches = {'occluded but lit': (occluded & ~dark, occluded),
                      'visible but dark': (visible & dark & ~reflects_nothing(base_color, metallic), visible)}
        details, passed = [], invalid == 0
        for mismatch_name, (mismatch, region) in mismatches.items():
            fraction = float(mismatch.sum()) / max(int(region.sum()), 1)
            cluster = largest_cluster(mismatch) if fraction <= MISMATCH_FRACTION else None
            passed = passed and region.sum() >= MIN_REGION_PIXELS and cluster is not None and cluster <= MAX_MISMATCH_CLUSTER
            details.append(f'{mismatch_name} {int(mismatch.sum())} of {int(region.sum())} ({fraction:.1e}), largest cluster {cluster}')
        check(f'{label} {name}: the direct light follows Light Visibility but for scattered sub-pixel gaps', passed,
              '; '.join(details) + f'; {invalid} invalid samples')
    set_light(session, default_light)


def check_bounces(session, check, start_pose, label, settings):
    """[P7]. settings 는 DIRECT 에 더할 PT 설정이다."""
    session.ask_ok(f'pose {start_pose}')
    images = [accumulate_export(session, DIRECT | settings | {'Max Bounces': bounces}, f'sponza-bounces-{bounces}')[0][..., :3]
              for bounces in BOUNCE_STEPS]
    # 양수 float 는 bit 를 정수로 읽은 차가 ulp 수다.
    darker = [int(np.count_nonzero(np.any(earlier.view(np.int32) - later.view(np.int32) > PT_SAMPLES, axis=2)))
              for earlier, later in zip(images, images[1:])]
    means = [float(image.mean()) for image in images]
    check(f'{label} raising Max Bounces through {BOUNCE_STEPS} darkens no pixel by more than {PT_SAMPLES} ulps and raises the mean each time',
          not any(darker) and all(later > earlier for earlier, later in zip(means, means[1:])),
          f'darker pixels {darker}, means {[round(mean, 6) for mean in means]}')
