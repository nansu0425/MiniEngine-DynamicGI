"""Sponza 에서 PT 의 directional light 직접광이 Light Visibility 를 따르고 반사가 더하는 빛과 어긋나지 않는지 보는 판정 ([P6], [P7]).

check-directional-light.py 가 Shading Lambert 로, check-gltf-brdf.py 가 Shading glTF 로 같은 판정을 하도록 여기에 모은다. 기준의
뜻과 임계값의 근거는 check-directional-light.py 의 docstring 에 있다.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import check_in_measurement_scenes
from imagefiles import read_dds_rgba32f
from imagemasks import eroded
from pathtracerchecks import BASE, SKY_RADIANCE, set_path_tracer
from viewer import SETTLE_FRAMES, capture

LIGHT_OBJECT = 'Directional Light'
EDGE_PIXELS = 2
# [P6] 의 low 를 찾을 때 보는 light 방향. 처음 방향 (고도 30, 방위 180) 은 harness baseline 의 시점에 보이는 곳과 가려진 곳이 둘 다 있어
# low 로 골랐던 방향이다. harness baseline 의 시점에서 고도 20 은 모든 방위에서 보이는 곳이 없었다.
LOW_LIGHT_ELEVATIONS = [30, 40, 50, 60]
LOW_LIGHT_AZIMUTHS = [180, 270, 0, 90]
PT_SAMPLES = 4
MIN_REGION_PIXELS = 1000
# 측정 scene 세 구도 (sponza-color-bleeding, sponza-fireflies, sponza-overhead-light) 의 default 와 low 에서 Lambert 와 glTF 로 잰
# 최댓값의 두 배다. 비율은 sponza-overhead-light 의 default 에서 가려졌는데 밝은 pixel 이 7.5e-5 로 가장 컸고, 보이는데 어두운 pixel 은
# 3.8e-5 까지였다. 덩어리는 sponza-color-bleeding 의 default 에서 가려졌는데 밝은 19 pixel 이 가장 컸다. 19 pixel 의 덩어리는 빛의 줄이다.
# 줄의 폭은 1 pixel 보다 좁고 pixel 가운데 사이로 지나서, camera 를 1/4 pixel 돌리면 Light Visibility 에도 흰 줄로 나타났다. harness
# baseline 의 시점에서는 비율이 3.4e-5 와 1.9e-5, 덩어리가 4 pixel 이었다.
MISMATCH_FRACTION = 1.5e-4
MAX_MISMATCH_CLUSTER = 38
BOUNCE_STEPS = [1, 2, 4]
WAIT_TIMEOUT = 900
DIRECT_LIGHT_CRITERION = 'the direct light follows Light Visibility but for scattered sub-pixel gaps'
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


def light_regions(session, name):
    """Light Visibility view 의 흰색 (보임) 과 검정 (가려짐) 을 가장자리 EDGE_PIXELS 깎은 두 mask"""
    visibility = overlay(session, 'Debug: Light Visibility', name)
    return eroded(np.all(visibility == 255, axis=2), EDGE_PIXELS), eroded(np.all(visibility == 0, axis=2), EDGE_PIXELS)


def has_both_regions(visible, occluded):
    """[P6] 이 뜻을 가지려면 화면에 있어야 하는, 보이는 곳과 가려진 곳이 둘 다 있는가"""
    return visible.sum() >= MIN_REGION_PIXELS and occluded.sum() >= MIN_REGION_PIXELS


def find_low_light(session, scene_name):
    """[P6] 의 low 인 light set 의 인자. LOW_LIGHT_ELEVATIONS 의 고도마다 LOW_LIGHT_AZIMUTHS 의 방위를 차례로 보고, has_both_regions 인
    첫 방향이다. has_both_regions 인 방향이 없으면 None 이다. 끝나면 light 의 방향이 바뀌어 있다."""
    for elevation in LOW_LIGHT_ELEVATIONS:
        for azimuth in LOW_LIGHT_AZIMUTHS:
            request = f'elevation={elevation} azimuth={azimuth}'
            session.ask_ok(f'light set {request}')
            if has_both_regions(*light_regions(session, f'{scene_name}-low-{elevation}-{azimuth}-visibility')):
                return request
    return None


def check_sponza_direct(session, check, label, settings, reflects_nothing, keep):
    """[P6]. measurement_scenes() 의 scene 마다 scene 의 light (default) 와 find_low_light 의 방향 (low) 에서 판정하고, has_both_regions
    가 아니면 N/A 다. settings 는 DIRECT 에 더할 PT 설정이고, reflects_nothing(base_color, metallic) 은 Base Color 와 Metallic view 의
    rgb 에서 판정하는 Shading 의 f 가 0 이라 보이는데 어두워도 되는 pixel 의 mask 다. keep 은 쌓은 export 의 경로를 받는다."""
    def judge(session, check, scene_name):
        scene_light = session.ask_ok(f'editor get {LIGHT_OBJECT}')['values']
        low = find_low_light(session, scene_name)
        set_light(session, scene_light)
        judged = {f'{label} default': check_direct_light(session, check, f'{label} {scene_name} default', f'{scene_name}-default', '',
                                                         settings, reflects_nothing, keep)}
        if low is None:
            judged[f'{label} low'] = check.premised(f'{label} {scene_name} low: {DIRECT_LIGHT_CRITERION}', False, False,
                                                    f'no light of elevation {LOW_LIGHT_ELEVATIONS} and azimuth {LOW_LIGHT_AZIMUTHS} has '
                                                    f'{MIN_REGION_PIXELS} visible and occluded pixels each')
        else:
            session.ask_ok(f'light set {low}')
            judged[f'{label} low'] = check_direct_light(session, check, f'{label} {scene_name} low', f'{scene_name}-low', f'light {low}; ',
                                                        settings, reflects_nothing, keep)
        set_light(session, scene_light)
        return judged

    check_in_measurement_scenes(session, check, judge)


def check_direct_light(session, check, label, name, light, settings, reflects_nothing, keep):
    """지금 camera 와 light 에서 [P6] 을 판정하고 판정했는지 돌려준다. name 은 capture 이름의 앞이고, light 는 detail 의 앞에 붙일 light 의
    설명이다."""
    visible, occluded = light_regions(session, f'{name}-visibility')
    if not has_both_regions(visible, occluded):
        return check.premised(f'{label}: {DIRECT_LIGHT_CRITERION}', False, False,
                              f'{light}{int(visible.sum())} visible and {int(occluded.sum())} occluded pixels, {MIN_REGION_PIXELS} each needed')
    base_color = overlay(session, 'Debug: Base Color', f'{name}-base-color')
    metallic = overlay(session, 'Debug: Metallic', f'{name}-metallic')
    rgba, invalid, path = accumulate_export(session, DIRECT | {'Geometric Normals': 1} | settings, f'{name}-direct')
    keep(path)
    dark = np.all(rgba[..., :3] == 0.0, axis=2)
    mismatches = {'occluded but lit': (occluded & ~dark, occluded),
                  'visible but dark': (visible & dark & ~reflects_nothing(base_color, metallic), visible)}
    details, passed = [], invalid == 0
    for mismatch_name, (mismatch, region) in mismatches.items():
        fraction = float(mismatch.sum()) / max(int(region.sum()), 1)
        cluster = largest_cluster(mismatch) if fraction <= MISMATCH_FRACTION else None
        passed = passed and cluster is not None and cluster <= MAX_MISMATCH_CLUSTER
        details.append(f'{mismatch_name} {int(mismatch.sum())} of {int(region.sum())} ({fraction:.1e}), largest cluster {cluster}')
    return check.premised(f'{label}: {DIRECT_LIGHT_CRITERION}', True, passed, light + '; '.join(details) + f'; {invalid} invalid samples')


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
