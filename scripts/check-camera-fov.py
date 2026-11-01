"""scene file 이 정하는 camera 의 세로 FOV 를 열고, 바꾸고, 저장하고, 측정 조건의 줄로 되돌리는지, raster 와 ray 가 같은 FOV 로 그리는지 확인한다.

  python -I scripts/check-camera-fov.py check   # Debug 로 두 번 띄워 아래 기준을 모두 판정한다

번호는 작업 13 (b) 의 FOV 기능의 완료 기준 번호다. scene file 은 WORK_DIR 에 쓴 Cornell box 의 scene file 이다. model 의 배율과 camera 의
pose 는 측정용 scene file (SCENE) 의 것이고, 세로 FOV 만 scene file 마다 다르다. 정답은 SCENE 의 glTF 를 만든 Mitsuba scene 을
cornellbox.py 로 읽어 해석적으로 구하므로 python -I 로 돌린다. app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다.
하나라도 실패하면 exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만
남는지도 본다. FOV 를 bit 단위로 비교할 때 text 는 float32 로 읽는다.

  [FV1] scene file 의 [Camera] Vertical FOV 가 세로 FOV 를 radian 으로 담는다. FOV 를 담은 scene file (WITH_FOV) 로 띄우거나 실행 중에
        열면 state camera 의 vertical_fov 가 적은 값과 bit 단위로 같다. FOV 가 없는 scene file (WITHOUT_FOV) 로 띄우거나, 다른 FOV 에서
        열면 45° (XM_PIDIV4) 와 bit 단위로 같다. 0 이하, pi 이상, 숫자가 아닌 FOV 를 담은 scene file (UNOPENABLE_FOVS) 의 scene open 은
        error 이고 state 가 그대로다 (시간에 따라 바뀌는 값 밖).
  [FV2] editor get Camera 가 Vertical FOV 를 state camera 의 vertical_fov 와 같은 text 로 준다. editor set Camera 로 바꾸면 state camera 와
        editor get 이 그 값이고 pose 는 그대로다. 받을 수 없는 값 (REJECTED_FOVS) 의 editor set 은 error 이고 state 가 그대로다. Harness
        아래에 복사한 scene file 에 scene save 를 하면 Vertical FOV 줄이 생기고, 다른 scene 을 열었다가 다시 열면 저장한 FOV 와 bit 단위로
        같다. Details panel 이 FOV 를 degree 로 보이고 mouse 로 바꾸는 것은 사용자가 본다 (modelviewer skill 의 사용자 확인 목록).
  [FV3] describe 와 capture pt 의 줄이 vertical fov 로 state camera 의 vertical_fov 를 같은 text 로 담는다. 기본값과 다른 FOV 의 줄 L 과
        그 export E1 (Shading glTF, LINE_SAMPLES) 을 얻고, app 을 WITH_FOV 로 다시 띄워 L 을 restore 하면 state camera 의 위치, heading,
        pitch, vertical_fov 가 L 과 문자열로 같고, 같은 sample 수의 export E2 가 E1 과 rgba 의 bit 까지 같다. L 의 scene 은 WITHOUT_FOV
        라서 restore 가 scene 을 연 뒤에 줄의 FOV 를 둔다. vertical fov 를 뺀 L (FOV 를 정할 수 있기 전의 줄) 의 restore 는 FOV 를 45° 로
        되돌리고 나머지는 L 과 같다.
  [FV4] FOV 를 바꾸면 PT 의 resets 가 1 늘고 samples 가 처음부터 다시 쌓인다. 45° 와 다른 PICK_FOVS 마다 pick grid (GRID) 의 판정한 칸이
        check-cornell-box.py 의 [W2] 처럼 Mitsuba scene 의 면으로 구한 material, 거리, 위치와 같고, 판정한 칸에 Mitsuba scene 의 shape
        마다 1 칸 이상이 있다. 판정한 칸 중 면을 맞는 칸의 pick 에서 depth 가 raster depth 와 depth_match_tolerance() 안이다. raster 와
        ray 가 다른 FOV 로 그리면 화면 가운데 밖의 pixel 에서 다른 점을 보므로 depth 가 달라진다.
  [FV5] FOV 를 바꾸고 끝낸 run 의 user.ini 에 FOV 가 없고, Editor Camera 에는 Move Speed 만 있다.
"""

import argparse
import configparser
import os
import shutil
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cornellbox
from checks import Checks, stop_and_check
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import BASE, set_path_tracer
from pickanswers import CameraAnswers, grid_cells, pick_matches
from viewer import (CAMERA_OBJECT, HARNESS_DIR, SETTLE_FRAMES, VIEWER_DIR, HarnessError, Session, baseline_scene, depth_match_tolerance,
                    read_scene_file, scene_gltf, shots_dir, start_instance, stop_instance, without_time_varying)

SCENE = baseline_scene('CornellBox.ini')
WORK_DIR = HARNESS_DIR / 'camera-fov-check'
WITH_FOV = WORK_DIR / 'with-fov.ini'
WITHOUT_FOV = WORK_DIR / 'without-fov.ini'
SAVE_SCENE = WORK_DIR / 'save.ini'
FORTY_FIVE_DEGREES = np.float32(np.pi / 4)
FOV_IN_FILE = '0.523598776'
UNOPENABLE_FOVS = ['0', '-0.1', '3.14159265', '3.5', 'wide', '0.5,0.5', 'nan', 'inf']
SET_FOV = '0.9'
REJECTED_FOVS = ['0', '-0.5', '3.14159274', '4', 'wide']
SAVED_FOV = '0.436332313'
# 하나는 45° 보다 좁고 하나는 넓다. 둘 다 GRID 에서 light 가 판정한 칸에 들도록 판정할 칸을 미리 구해 보고 골랐다.
PICK_FOVS = ['0.6', '1']
GRID = (81, 50)
# [FV3] 의 줄의 조건. 세로 FOV 는 Mitsuba 의 Cornell box sensor 의 19.5° 다.
LINE_POSE = '150 400 2500 0.2 0.05'
LINE_FOV = '0.340339203'
LINE_PATH_TRACER = {'Shading': 'glTF', 'Output': 'Radiance', 'Seed': 3, 'Max Bounces': 6, 'Russian Roulette': 1, 'Geometric Normals': 0}
LINE_SAMPLES = 16
RESET_SAMPLES = 4 * SETTLE_FRAMES + 8
WAIT_TIMEOUT = 900


def parse_line(line):
    return dict(item.split('=', 1) for item in line.split('; '))


def format_line(values):
    return '; '.join(f'{key}={value}' for key, value in values.items())


def bits(text):
    return np.float32(text).view(np.uint32)


def viewer_path(path):
    return Path(path).relative_to(VIEWER_DIR).as_posix()


def write_scene(path, vertical_fov):
    """SCENE 의 model 과 camera pose 에 vertical_fov 를 Vertical FOV 로 둔 scene file 을 쓴다. vertical_fov 가 None 이면 그 줄이 없다."""
    source = read_scene_file(SCENE)
    gltf = Path(os.path.relpath(VIEWER_DIR / scene_gltf(SCENE), path.parent)).as_posix()
    camera = {key: source[CAMERA_OBJECT][key] for key in ('Position', 'Heading', 'Pitch')}
    if vertical_fov is not None:
        camera['Vertical FOV'] = vertical_fov
    path.write_text(f'; check-camera-fov.py 의 scene file\n[Model]\nglTF={gltf}\nScale={source["Model"]["Scale"]}\n\n[{CAMERA_OBJECT}]\n' +
                    ''.join(f'{key}={value}\n' for key, value in camera.items()), encoding='utf-8')
    return viewer_path(path)


def write_scenes():
    """WITH_FOV, WITHOUT_FOV 와 UNOPENABLE_FOVS 의 scene file 을 쓰고, 뒤의 것은 (FOV, MiniEngine/ModelViewer 기준 경로) 로 돌려준다."""
    if WORK_DIR.exists():
        shutil.rmtree(WORK_DIR)
    WORK_DIR.mkdir(parents=True)
    write_scene(WITH_FOV, FOV_IN_FILE)
    write_scene(WITHOUT_FOV, None)
    return [(fov, write_scene(WORK_DIR / f'unopenable-{index}.ini', fov)) for index, fov in enumerate(UNOPENABLE_FOVS)]


def camera_state(session):
    return session.ask_ok('state camera')['state']['camera']


def check_opened_fovs(session, check, unopenable):
    """[FV1] 의 실행 중 scene open"""
    session.ask_ok(f'scene open {viewer_path(WITH_FOV)}')
    with_fov = camera_state(session)['vertical_fov']
    session.ask_ok(f'scene open {viewer_path(WITHOUT_FOV)}')
    without_fov = camera_state(session)['vertical_fov']
    check(f'[FV1] scene open of the scene file with Vertical FOV={FOV_IN_FILE} gives it, and then of the one without gives 45 degrees, '
          'both to the bit', bits(with_fov) == bits(FOV_IN_FILE) and bits(without_fov) == FORTY_FIVE_DEGREES.view(np.uint32),
          f'vertical_fov {with_fov}, then {without_fov}')

    wrong = []
    for fov, scene in unopenable:
        before = without_time_varying(session.ask_ok('state')['state'])
        reply = session.ask(f'scene open {scene}')
        after = without_time_varying(session.ask_ok('state')['state'])
        if reply['ok'] or after != before:
            wrong.append(f'Vertical FOV={fov}: {"ok" if reply["ok"] else reply["error"]}{", state changed" if after != before else ""}')
    check('[FV1] scene open of a scene file whose Vertical FOV is 0 or less, pi or more, or not a number fails and changes no state', not wrong,
          '; '.join(wrong) or ', '.join(fov for fov, _ in unopenable))


def check_editor_fov(session, check):
    """[FV2] 의 editor get 과 editor set"""
    before = camera_state(session)
    got = session.ask_ok(f'editor get {CAMERA_OBJECT}')['values']
    check('[FV2] editor get Camera gives Vertical FOV as state camera gives vertical_fov', got.get('Vertical FOV') == before['vertical_fov'],
          f'editor get {got.get("Vertical FOV")}, state {before["vertical_fov"]}')

    reply = session.ask(f'editor set {CAMERA_OBJECT}: Vertical FOV={SET_FOV}')
    after = camera_state(session)
    got = session.ask_ok(f'editor get {CAMERA_OBJECT}')['values']
    pose = [key for key in ['x', 'y', 'z', 'heading', 'pitch'] if after[key] != before[key]]
    check(f'[FV2] editor set Camera: Vertical FOV={SET_FOV} sets state camera and editor get to it and keeps the pose',
          reply['ok'] and bits(after['vertical_fov']) == bits(SET_FOV) and got['Vertical FOV'] == after['vertical_fov'] and not pose,
          reply.get('error') or f'vertical_fov {after["vertical_fov"]}, editor get {got["Vertical FOV"]}, pose changed in {pose or "none"}')

    wrong = []
    for fov in REJECTED_FOVS:
        before = without_time_varying(session.ask_ok('state')['state'])
        reply = session.ask(f'editor set {CAMERA_OBJECT}: Vertical FOV={fov}')
        after = without_time_varying(session.ask_ok('state')['state'])
        if reply['ok'] or after != before:
            wrong.append(f'{fov}: {"ok" if reply["ok"] else "error"}{", state changed" if after != before else ""}')
    check('[FV2] editor set of a Vertical FOV that is 0 or less, pi or more, or not a number fails and changes no state', not wrong,
          '; '.join(wrong) or ', '.join(REJECTED_FOVS))


def check_saved_fov(session, check):
    """[FV2] 의 scene save"""
    shutil.copy2(WITHOUT_FOV, SAVE_SCENE)
    session.ask_ok(f'scene open {viewer_path(SAVE_SCENE)}')
    session.ask_ok(f'editor set {CAMERA_OBJECT}: Vertical FOV={SAVED_FOV}')
    saved = session.ask_ok(f'editor get {CAMERA_OBJECT}')['values']['Vertical FOV']
    session.ask_ok('scene save')
    written = read_scene_file(viewer_path(SAVE_SCENE))[CAMERA_OBJECT].get('Vertical FOV')
    session.ask_ok(f'scene open {viewer_path(WITHOUT_FOV)}')
    session.ask_ok(f'scene open {viewer_path(SAVE_SCENE)}')
    reopened = camera_state(session)['vertical_fov']
    check(f'[FV2] scene save writes Vertical FOV to a scene file without it, and opening it again gives the saved {SAVED_FOV} to the bit',
          written == saved and bits(reopened) == bits(SAVED_FOV), f'written {written}, saved {saved}, reopened {reopened}')


def pt_progress(session):
    return session.ask_ok('state pt')['state']['pt']


def check_reset(session, check, fov):
    """[FV4] 의 PT 다시 쌓기"""
    set_path_tracer(session, BASE | {'Shading': 'glTF', 'Target Samples': 1 << 20})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={RESET_SAMPLES}', timeout=WAIT_TIMEOUT)
    before = pt_progress(session)
    session.ask_ok(f'editor set {CAMERA_OBJECT}: Vertical FOV={fov}')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    after = pt_progress(session)
    set_path_tracer(session, {'Show': 0})
    check(f'[FV4] changing the vertical FOV to {fov} starts the path tracer over once',
          after['resets'] == before['resets'] + 1 and after['samples'] < before['samples'],
          f'resets {before["resets"]} -> {after["resets"]}, samples {before["samples"]} -> {after["samples"]}')


def check_picks(session, check, faces, fov):
    """[FV4] 의 pick grid 와 raster depth. 지금 FOV 가 fov 다."""
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    answers = CameraAnswers(faces, session.ask_ok('state')['state'])
    grid = grid_cells(session.ask_ok(f'pick grid {GRID[0]}x{GRID[1]}')['grid'])
    wrong, judged, face_cells = [], {}, []
    for x, y, values in grid:
        if not answers.unambiguous((x, y)):
            continue
        origin, direction, found = answers.expect(np.array([x, y]) + 0.5)
        seen = 'sky' if found is None else found[1].shape
        judged[seen] = judged.get(seen, 0) + 1
        if not pick_matches(values, origin, direction, found):
            wrong.append(f'{x},{y}: {values} where ' + ('nothing is' if found is None else f'{seen} is at {found[0]}'))
        elif found is not None:
            face_cells.append((x, y))
    shapes = sorted({face.shape for face in faces})
    lacking = [shape for shape in shapes if judged.get(shape, 0) == 0]
    check(f'[FV4] at vertical FOV {fov}, pick grid finds the material, distance and position of the Mitsuba scene in each judged cell, with '
          'judged cells on every shape', not wrong and not lacking,
          '; '.join(wrong[:3]) + (f'; no judged cell on {lacking}' if lacking else '') or f'judged cells {judged}')

    tolerance = depth_match_tolerance()
    mismatched = []
    for x, y in face_cells:
        pick = session.ask_ok(f'pick {x} {y}')['pick']
        raster = pick['raster']
        error = abs(float(pick['depth']) - float(raster['depth'])) / float(raster['depth']) if raster['hit'] else None
        if error is None or error > tolerance:
            mismatched.append(f'{x},{y}: depth {pick["depth"]}, raster {raster}')
    check(f'[FV4] at vertical FOV {fov}, the pick depth of each judged cell on a face is its raster depth within {tolerance:g}',
          face_cells and not mismatched, '; '.join(mismatched[:3]) or f'{len(face_cells)} cells')


def take_line(session):
    """[FV3] 의 (줄, export)"""
    session.ask_ok(f'scene open {viewer_path(WITHOUT_FOV)}')
    session.ask_ok(f'pose {LINE_POSE}')
    session.ask_ok(f'editor set {CAMERA_OBJECT}: Vertical FOV={LINE_FOV}')
    set_path_tracer(session, BASE | LINE_PATH_TRACER | {'Target Samples': LINE_SAMPLES})
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={LINE_SAMPLES}', timeout=WAIT_TIMEOUT)
    reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / "line"}')
    return reply['line'], read_dds_rgba32f(reply['path'])


def check_line_fov(session, check, line):
    """[FV3] 의 줄"""
    fov = camera_state(session)['vertical_fov']
    described = parse_line(session.ask_ok('describe')['line']).get('vertical fov')
    captured = parse_line(line).get('vertical fov')
    check('[FV3] the lines of capture pt and describe have the vertical FOV of state camera as vertical fov',
          captured == described == fov and bits(fov) == bits(LINE_FOV), f'capture pt {captured}, describe {described}, state {fov}')


def first_run(check, no_build, unopenable, faces):
    """[FV1], [FV2], [FV4] 를 판정하고 [FV3] 의 (줄, export) 와 [FV5] 의 user.ini 를 돌려준다."""
    instance = start_instance('Debug', scene=viewer_path(WITHOUT_FOV), build_first=not no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            started = camera_state(session)['vertical_fov']
            check('[FV1] started with the scene file without Vertical FOV, the vertical FOV is 45 degrees to the bit',
                  bits(started) == FORTY_FIVE_DEGREES.view(np.uint32), started)
            check_opened_fovs(session, check, unopenable)
            check_editor_fov(session, check)
            check_saved_fov(session, check)
            for fov in PICK_FOVS:
                check_reset(session, check, fov)
                check_picks(session, check, faces, fov)
            line, image = take_line(session)
            check_line_fov(session, check, line)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise
    return line, image, Path(instance['run_dir']) / 'user.ini'


def check_user_settings(check, path):
    """[FV5]"""
    text = path.read_text(encoding='utf-8')
    ini = configparser.ConfigParser(delimiters=('=',), interpolation=None)
    ini.optionxform = str
    ini.read_string(text)
    keys = list(ini['Editor Camera']) if ini.has_section('Editor Camera') else None
    lines = [line for line in text.splitlines() if 'fov' in line.lower()]
    check('[FV5] the user.ini of a run that changed the vertical FOV has no FOV and only Move Speed in Editor Camera',
          not lines and keys == ['Move Speed'], f'lines with FOV {lines or "none"}; Editor Camera {keys}')


def check_restored(session, check, line, image):
    """[FV3] 의 restore"""
    session.ask_ok(f'restore {line}')
    camera = camera_state(session)
    values = parse_line(line)
    restored = [str(camera[key]) for key in ['x', 'y', 'z', 'heading', 'pitch', 'vertical_fov']]
    expected = values['camera'].split(',') + [values['heading'], values['pitch'], values['vertical fov']]
    check('[FV3] after a restart, restore of the line gives its camera and vertical FOV in state camera as the same text',
          restored == expected, f'{" ".join(restored)} vs {" ".join(expected)}')
    set_path_tracer(session, {'Show': 1})
    session.ask_ok(f'pt wait samples={LINE_SAMPLES}', timeout=WAIT_TIMEOUT)
    again = read_dds_rgba32f(session.ask_ok(f'capture pt {shots_dir(session.instance) / "line-restarted"}')['path'])
    differing = int(np.count_nonzero(np.any(again.view(np.uint32) != image.view(np.uint32), axis=2))) if again.shape == image.shape else None
    check(f'[FV3] the export after the restart is bit-identical to the one before, at {LINE_SAMPLES} samples of Shading glTF', differing == 0,
          f'{differing} pixels differ' if differing is not None else f'shape {again.shape} vs {image.shape}')

    old = {key: value for key, value in values.items() if key != 'vertical fov'}
    reply = session.ask(f'restore {format_line(old)}')
    fov = camera_state(session)['vertical_fov']
    now = parse_line(session.ask_ok('describe')['line'])
    differing = [key for key in old if key != 'pt samples' and now.get(key) != old[key]]
    check('[FV3] restore of the line without vertical fov, as a line from before the vertical FOV could be set, gives 45 degrees and the rest',
          reply['ok'] and bits(fov) == FORTY_FIVE_DEGREES.view(np.uint32) and not differing,
          reply.get('error') or f'vertical_fov {fov}; differing from the line: {", ".join(differing) or "none"}')


def second_run(check, line, image):
    """[FV1] 의 시작과 [FV3] 의 restore"""
    instance = start_instance('Debug', scene=viewer_path(WITH_FOV), build_first=False)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            started = camera_state(session)['vertical_fov']
            check(f'[FV1] started with the scene file with Vertical FOV={FOV_IN_FILE}, the vertical FOV is it to the bit',
                  bits(started) == bits(FOV_IN_FILE), started)
            check_restored(session, check, line, image)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise


def command_check(args):
    check = Checks()
    try:
        mitsuba = cornellbox.read(cornellbox.written_source(cornellbox.written_gltf(VIEWER_DIR / SCENE)))
        faces = cornellbox.world_faces(mitsuba, float(read_scene_file(SCENE)['Model']['Scale']))
        unopenable = write_scenes()
        line, image, user_settings = first_run(check, args.no_build, unopenable, faces)
        check_user_settings(check, user_settings)
        second_run(check, line, image)
    except (HarnessError, ImageFileError, cornellbox.SceneError) as error:
        check('check ran to the end', False, str(error))
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('check', help='start Debug twice and judge the criteria')
    command.add_argument('--no-build', action='store_true')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
