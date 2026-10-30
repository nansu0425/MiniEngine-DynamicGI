"""실행 중에 연 scene 이 처음부터 그 scene 으로 띄운 app 과 같은 상태와 그림인지 확인한다.

  python scripts/check-scene-open.py check   # Debug 로 띄워 아래 기준을 모두 판정한다

scene 은 `scene open <scene file>` 명령으로 연다. 이 명령과 Scene menu 는 ModelViewer::OpenScene 을 함께 쓴다. 번호는 실행 중 scene
열기의 완료 기준 번호다. scene 은 SCENES 둘이다. app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면
exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다.

scene 마다 처음부터 그 scene 으로 띄운 app 의 state, harness baseline 의 raster scene capture, PT export (PT_SETTINGS) 를 기준으로 남기고,
Sponza 로 띄운 app 에서 scene 을 바꿔 가며 비교한다.

  [O1] scene 을 연 뒤 state 의 scene, model, camera 가 처음부터 그 scene 으로 띄운 app 과 문자열로 같고, light, sky, settings 는 열기 전과
       문자열로 같다. Sponza 에서 Cornell box 로, Cornell box 에서 Sponza 로 연다. Cornell box 로 띄운 app 에서 Sponza 를 처음 여는
       경우도 본다. 띄운 scene 보다 mesh 가 많은 scene 을 처음 여는 경우다.
  [O2] scene 을 연 뒤의 raster scene capture 와 PT export 의 rgba 가 처음부터 그 scene 으로 띄운 app 의 것과 bit 단위로 같다. 두 방향 모두 잰다.
  [O3] scene 을 열면 PT 의 resets 가 1 늘고 samples 가 처음부터 다시 쌓인다 (연 뒤 SETTLE_FRAMES 의 samples 가 열기 전보다 적다).
  [O4] 두 scene 을 ROUND_TRIPS 번 오간 뒤에도 [O2] 가 성립하고, D3D12 의 CORRUPTION, ERROR, WARNING 이 0 이며, state renderer 의 scene
       texture descriptor 사용량이 두 scene 을 한 번씩 연 뒤와 같다.
  [O5] 열 수 없는 scene file (없는 파일, 형식이 틀린 파일, glTF 가 없는 파일) 의 scene open 은 error 이고 state 가 열기 전과 같다
       (시간에 따라 바뀌는 값 밖).
  [O9] scene 을 바꾸고 끝낸 run 의 editor.ini 에 scene 이 없고, 그 editor.ini 로 -scene 없이 띄우면 Sponza 다.
"""

import argparse
import shutil
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import BASE, SECTION, set_path_tracer
from viewer import (HARNESS_DIR, SETTLE_FRAMES, VIEWER_DIR, HarnessError, Session, capture, default_scene, start_instance, stop_instance,
                    without_time_varying)

SPONZA = 'Scenes/Sponza.ini'
CORNELL_BOX = 'Scenes/CornellBox.ini'
SCENES = [SPONZA, CORNELL_BOX]
WORK_DIR = HARNESS_DIR / 'scene-open-check'
PT_SAMPLES = 16
PT_SETTINGS = BASE | {'Shading': 'glTF', 'Seed': 0, 'Target Samples': PT_SAMPLES}
ROUND_TRIPS = 20
WAIT_TIMEOUT = 900
# scene 을 열면 바뀌는 state 와 그대로인 state
OPENED_STATE = ['scene', 'model', 'camera']
KEPT_STATE = ['light', 'sky', 'settings']


def state_of(session, groups):
    state = session.ask_ok('state')['state']
    return {group: state[group] for group in groups}


def pictures(session, name):
    """(PT 를 끄고 찍은 raster scene capture, PT_SETTINGS 로 쌓은 PT export)"""
    set_path_tracer(session, {'Show': 0})
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    raster = read_dds_rgba32f(capture(session, 'scene', f'{name}-raster'))
    set_path_tracer(session, PT_SETTINGS)
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={PT_SAMPLES}', timeout=WAIT_TIMEOUT)
    exported = read_dds_rgba32f(capture(session, 'pt', f'{name}-pt'))
    set_path_tracer(session, {'Show': 0})
    return raster, exported


def differing_pixels(first, second):
    if first.shape != second.shape:
        return f'shape {first.shape} vs {second.shape}'
    return int(np.count_nonzero(np.any(first.view(np.uint32) != second.view(np.uint32), axis=2)))


def fresh_run(check, scene, build_first):
    """처음부터 scene 으로 띄운 app 의 (state, raster, PT export)"""
    instance = start_instance('Debug', scene=scene, build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            state = state_of(session, OPENED_STATE)
            raster, exported = pictures(session, f'fresh-{Path(scene).stem}')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise
    return {'state': state, 'raster': raster, 'pt': exported}


def check_opened(session, check, scene, fresh, label):
    """[O1], [O2], [O3]: 지금 scene 에서 scene 을 연다."""
    set_path_tracer(session, PT_SETTINGS | {'Target Samples': 1 << 20})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={4 * SETTLE_FRAMES + 8}', timeout=WAIT_TIMEOUT)
    kept = state_of(session, KEPT_STATE)
    before = session.ask_ok('state pt')['state']['pt']
    reply = session.ask_ok(f'scene open {scene}')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    after = session.ask_ok('state pt')['state']['pt']
    opened, still = state_of(session, OPENED_STATE), state_of(session, KEPT_STATE)
    check(f'[O3] {label}: the path tracer starts over once', after['resets'] == before['resets'] + 1 and after['samples'] < before['samples'],
          f'resets {before["resets"]} -> {after["resets"]}, samples {before["samples"]} -> {after["samples"]}')
    differing = [group for group in OPENED_STATE if opened[group] != fresh[scene]['state'][group]]
    replied = [group for group in OPENED_STATE if reply['state'][group] != opened[group]]
    changed = [group for group in KEPT_STATE if still[group] != kept[group]]
    check(f'[O1] {label}: state scene, model and camera are as when started with -scene {scene}, and light, sky and settings stay',
          not differing and not replied and not changed,
          f'differ from the fresh app: {differing or "none"}; reply differs: {replied or "none"}; changed: {changed or "none"}')
    check_pictures(session, check, scene, fresh, label)


def check_pictures(session, check, scene, fresh, label):
    """[O2]"""
    raster, exported = pictures(session, f'{label.replace(" ", "-")}-{Path(scene).stem}')
    raster_pixels, pt_pixels = differing_pixels(raster, fresh[scene]['raster']), differing_pixels(exported, fresh[scene]['pt'])
    check(f'[O2] {label}: the raster scene capture and the path tracer export are bit-identical to the app started with -scene {scene}',
          raster_pixels == 0 and pt_pixels == 0, f'raster {raster_pixels} pixels differ, path tracer {pt_pixels} pixels differ')


def descriptors(session):
    return session.ask_ok('state renderer')['state']['renderer']['scene_texture_descriptors']['allocated']


def check_round_trips(session, check, fresh):
    """[O4]. SPONZA 와 CORNELL_BOX 를 한 번씩 연 뒤에 부른다."""
    allocated = descriptors(session)
    for _ in range(ROUND_TRIPS):
        for scene in (CORNELL_BOX, SPONZA):
            session.ask_ok(f'scene open {scene}')
    after = descriptors(session)
    d3d12 = session.ask_ok('status')['d3d12_messages']
    check(f'[O4] after {ROUND_TRIPS} round trips the scene texture descriptors in use are as after opening each scene once, '
          'with no D3D12 corruption, error or warning', after == allocated and d3d12['corruption'] == d3d12['error'] == d3d12['warning'] == 0,
          f'{allocated} -> {after}; d3d12 {d3d12}')
    for scene in (CORNELL_BOX, SPONZA):
        session.ask_ok(f'scene open {scene}')
        session.ask_ok(f'frames {SETTLE_FRAMES}')
        check_pictures(session, check, scene, fresh, f'after {ROUND_TRIPS} round trips')


def write_unopenable():
    """[O5] 의 scene file. (이름, MiniEngine/ModelViewer 기준 경로) 의 목록이다."""
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    (WORK_DIR / 'no-camera.ini').write_text('[Model]\nglTF=missing.gltf\nScale=1\n', encoding='utf-8')
    (WORK_DIR / 'missing-gltf.ini').write_text('[Model]\nglTF=missing.gltf\nScale=1\n\n[Camera]\nFit=Model\n', encoding='utf-8')
    absent = WORK_DIR / 'absent.ini'
    if absent.exists():
        absent.unlink()
    return [(name, (WORK_DIR / file).relative_to(VIEWER_DIR).as_posix())
            for name, file in [('a missing file', 'absent.ini'), ('a file without [Camera]', 'no-camera.ini'), ('a file whose glTF is missing', 'missing-gltf.ini')]]


def check_unopenable(session, check):
    """[O5]"""
    wrong = []
    for name, scene in write_unopenable():
        before = without_time_varying(session.ask_ok('state')['state'])
        reply = session.ask(f'scene open {scene}')
        after = without_time_varying(session.ask_ok('state')['state'])
        if reply['ok'] or after != before:
            wrong.append(f'{name}: {"ok" if reply["ok"] else reply["error"]}{", state changed" if after != before else ""}')
        else:
            print(f'      {name}: {reply["error"]}')
    check('[O5] scene open of a scene file that cannot be opened fails and changes no state', not wrong, '; '.join(wrong) or 'three files')


def started_in_cornell_box_run(check, fresh):
    """[O1], [O2], [O3]: Cornell box 로 띄운 app 에서 Sponza 를 처음 연다."""
    instance = start_instance('Debug', scene=CORNELL_BOX, build_first=False)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            check_opened(session, check, SPONZA, fresh, 'started in the Cornell box to Sponza')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise


def switching_run(check, fresh):
    """Sponza 로 띄워 scene 을 바꿔 가며 판정하고, 끝낸 run 의 폴더를 돌려준다."""
    instance = start_instance('Debug', scene=SPONZA, build_first=False)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            check_opened(session, check, CORNELL_BOX, fresh, 'Sponza to the Cornell box')
            check_opened(session, check, SPONZA, fresh, 'the Cornell box to Sponza')
            check_round_trips(session, check, fresh)
            check_unopenable(session, check)
            session.ask_ok(f'scene open {CORNELL_BOX}')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise
    return Path(instance['run_dir'])


def check_saved_scene(check, run_dir):
    """[O9]"""
    saved = (run_dir / 'editor.ini').read_text(encoding='utf-8')
    names = [name for name in SCENES + [Path(scene).stem for scene in SCENES] if name in saved]
    baseline = WORK_DIR / 'after-switching'
    if baseline.exists():
        shutil.rmtree(baseline)
    baseline.mkdir(parents=True)
    for settings in run_dir.glob('*.ini'):
        shutil.copy2(settings, baseline / settings.name)
    instance = start_instance('Debug', build_first=False, baseline=baseline)
    try:
        session = Session(instance)
        try:
            scene = session.ask_ok('state scene')['state']['scene']['path']
        finally:
            session.close()
        stop_and_check(instance, check)
    except HarnessError:
        stop_instance(instance, kill_now=True)
        raise
    check('[O9] the editor.ini of a run that ended in the Cornell box names no scene, and the app started with it opens Sponza',
          not names and scene == default_scene() == SPONZA, f'names in editor.ini: {names or "none"}; opened {scene}')


def command_check(args):
    check = Checks()
    try:
        fresh = {scene: fresh_run(check, scene, not args.no_build and scene == SCENES[0]) for scene in SCENES}
        started_in_cornell_box_run(check, fresh)
        run_dir = switching_run(check, fresh)
        check_saved_scene(check, run_dir)
    except (HarnessError, ImageFileError) as error:
        check('check ran to the end', False, str(error))
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('check', help='start Debug and judge the criteria')
    command.add_argument('--no-build', action='store_true')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
