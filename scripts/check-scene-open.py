"""실행 중에 연 scene 이 처음부터 그 scene 으로 띄운 app 과 같은 상태와 그림인지, 물체의 설정을 scene file 에서 읽고 저장하는지 확인한다.

  python scripts/check-scene-open.py check   # Debug 로 띄워 아래 기준을 모두 판정한다

scene 은 `scene open <scene file>` 명령으로 연다. 이 명령과 Scene menu 는 ModelViewer::OpenScene 을 함께 쓴다. 번호는 실행 중 scene
열기의 완료 기준 번호다. scene 은 SCENES 둘이다. app 을 직접 띄우고 끝내며, 이미 띄운 harness instance 는 먼저 끝낸다. 하나라도 실패하면
exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE 하나만 남는지도 본다.

scene 마다 처음부터 그 scene 으로 띄운 app 의 state, harness baseline 의 raster scene capture, PT export (PT_SETTINGS) 를 기준으로 남기고,
Sponza 로 띄운 app 에서 scene 을 바꿔 가며 비교한다. [O10], [O11] 은 WORK_DIR 에 쓴 scene file 로 본다. Scenes 의 scene file 에는
저장하지 않는다.

  [O1] scene 을 연 뒤 state 의 scene, model, camera, light, sky 가 처음부터 그 scene 으로 띄운 app 과 문자열로 같고, settings 는 열기
       전과 문자열로 같다. 열기 전에 light 와 sky 를 바꿔 두므로, 저장하지 않은 물체의 설정이 버려지는지도 본다. Sponza 에서
       Cornell box 로, Cornell box 에서 Sponza 로 연다. Cornell box 로 띄운 app 에서 Sponza 를 처음 여는
       경우도 본다. 띄운 scene 보다 mesh 가 많은 scene 을 처음 여는 경우다.
  [O2] scene 을 연 뒤의 raster scene capture 와 PT export 의 rgba 가 처음부터 그 scene 으로 띄운 app 의 것과 bit 단위로 같다. 두 방향 모두 잰다.
  [O3] scene 을 열면 PT 의 resets 가 1 늘고 samples 가 처음부터 다시 쌓인다 (연 뒤 SETTLE_FRAMES 의 samples 가 열기 전보다 적다).
  [O4] 두 scene 을 ROUND_TRIPS 번 오간 뒤에도 [O2] 가 성립하고, D3D12 의 CORRUPTION, ERROR, WARNING 이 0 이며, state renderer 의 scene
       texture descriptor 사용량이 두 scene 을 한 번씩 연 뒤와 같다.
  [O5] 열 수 없는 scene file (없는 파일, 형식이 틀린 파일, glTF 가 없는 파일) 의 scene open 은 error 이고 state 가 열기 전과 같다
       (시간에 따라 바뀌는 값 밖).
  [O6] scene 을 연 뒤 describe 의 scene 이 연 scene file 이다. 처음부터 Cornell box 로 띄워 얻은 줄 L 을 Sponza 에서 restore 하면 Cornell
       box 를 열고, state camera 가 L 과 문자열로 같으며, PT_SAMPLES 의 PT export 가 처음부터 Cornell box 로 띄워 L 을 restore 한 export 와
       rgba 의 bit 까지 같다. Sponza 의 줄과 scene 이 없는 예전 줄을 Cornell box 에서 restore 하면 Sponza 를 열고 되돌린다.
  [O7] 다른 scene 의 줄이라도 key 가 빠졌거나 받을 수 없는 값이 있으면 error 이고 scene 을 바꾸지 않으며 state 가 그대로다 (시간에 따라
       바뀌는 값 밖). 줄의 scene file 을 열 수 없으면 error 이고 unread 에 scene 이 있다.
  [O9] scene 을 바꾸고 끝낸 run 의 user.ini 에 scene 과 light, sky 가 없고, 그 user.ini 로 -scene 없이 띄우면 Sponza 다.
  [O10] scene file 의 물체 이름 section 에 기본값과 다른 OBJECT_SETTINGS 를 둔 scene 을 열면 editor get 이 그 값을 글자 그대로 주고,
       section 이 없는 scene 을 열면 그 scene 으로 띄운 app 의 값 (기본값) 을 준다. 바꾸고 저장하지 않은 값은 scene 을 다시 열면
       버려진다. scene 에 없는 물체의 editor get 은 error 다.
  [O11] scene save 는 바꾼 물체의 설정을 그 section 의 key 줄에만 쓰고 주석, [Model], [Camera] 와 CRLF 줄 끝은 그대로 두며, 다시 열면
       그 값이다. 물체의 section 이 없는 scene file 에는 그 끝에 section 을 추가하고 앞의 줄은 그대로 둔다.
"""

import argparse
import os
import shutil
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from checks import Checks, stop_and_check
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import BASE, SECTION, set_path_tracer
from viewer import (HARNESS_DIR, SETTLE_FRAMES, VIEWER_DIR, HarnessError, Session, capture, default_scene, read_scene_file, start_instance,
                    stop_instance, without_time_varying)

SPONZA = 'Scenes/Sponza.ini'
CORNELL_BOX = 'Scenes/CornellBox.ini'
SCENES = [SPONZA, CORNELL_BOX]
WORK_DIR = HARNESS_DIR / 'scene-open-check'
PT_SAMPLES = 16
PT_SETTINGS = BASE | {'Shading': 'glTF', 'Seed': 0, 'Target Samples': PT_SAMPLES}
ROUND_TRIPS = 20
WAIT_TIMEOUT = 900
# [O6] 의 줄 L 을 얻는 조건. 처음과 모든 조건이 다르다.
LINE_POSE = '150 400 2500 0.2 0.05'
LINE_LIGHT = 'elevation=25 azimuth=80 color=1,0.9,0.8 intensity=6'
LINE_SKY = 'Color=0.5,0.75,1; Intensity=0.25'
# scene 을 열면 바뀌는 state 와 그대로인 state
OPENED_STATE = ['scene', 'model', 'camera', 'light', 'sky']
KEPT_STATE = ['settings']
# [O10], [O11] 의 scene file. OBJECTS_SCENE 에만 물체의 section 이 있다.
OBJECTS_SCENE = WORK_DIR / 'objects.ini'
NO_OBJECTS_SCENE = WORK_DIR / 'no-objects.ini'
# OBJECTS_SCENE 의 물체의 설정. 기본값과 다르고, editor get 이 글자 그대로 다시 주는 값이다.
OBJECT_SETTINGS = {
    'Directional Light': {'Color': '0.75,0.5,0.25', 'Direction': '0,1,0', 'Intensity': '6', 'Position': '10,20,30', 'Up': '0,0,1'},
    'Sky Light': {'Color': '0.5,0.75,1', 'Intensity': '0.25'},
}


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


def parse_line(line):
    return dict(item.split('=', 1) for item in line.split('; '))


def format_line(values):
    return '; '.join(f'{key}={value}' for key, value in values.items())


def restored_export(session, line, name):
    """line 을 restore 하고 PT 를 PT_SAMPLES 까지 쌓은 export"""
    session.ask_ok(f'restore {line}')
    set_path_tracer(session, {'Show': 1})
    session.ask_ok(f'pt wait samples={PT_SAMPLES}', timeout=WAIT_TIMEOUT)
    exported = read_dds_rgba32f(capture(session, 'pt', name))
    set_path_tracer(session, {'Show': 0})
    return exported


def line_conditions(session):
    """[O6] 의 줄. PT_SETTINGS 로 쌓은 뒤의 describe 다."""
    session.ask_ok(f'pose {LINE_POSE}')
    session.ask_ok(f'light set {LINE_LIGHT}')
    session.ask_ok(f'editor set Sky Light: {LINE_SKY}')
    set_path_tracer(session, PT_SETTINGS)
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={PT_SAMPLES}', timeout=WAIT_TIMEOUT)
    return session.ask_ok('describe')['line']


def fresh_run(check, scene, build_first):
    """처음부터 scene 으로 띄운 app 의 (state, raster, PT export) 와, [O6] 의 줄과 그 줄을 restore 한 PT export"""
    instance = start_instance('Debug', scene=scene, build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            state = state_of(session, OPENED_STATE)
            raster, exported = pictures(session, f'fresh-{Path(scene).stem}')
            line = line_conditions(session)
            restored = restored_export(session, line, f'fresh-{Path(scene).stem}-line')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise
    return {'state': state, 'raster': raster, 'pt': exported, 'line': line, 'restored': restored}


def check_opened(session, check, scene, fresh, label):
    """[O1], [O2], [O3]: 지금 scene 에서 scene 을 연다."""
    # 저장하지 않은 물체의 설정은 연 scene file 의 값으로 바뀌어야 한다. PT 가 다시 쌓기 시작한 뒤에 [O3] 의 resets 를 세도록 먼저 바꾼다.
    session.ask_ok(f'light set {LINE_LIGHT}')
    session.ask_ok(f'editor set Sky Light: {LINE_SKY}')
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
    check(f'[O1] {label}: state scene, model, camera, light and sky are as when started with -scene {scene}, and settings stay',
          not differing and not replied and not changed,
          f'differ from the fresh app: {differing or "none"}; reply differs: {replied or "none"}; changed: {changed or "none"}')
    described = parse_line(session.ask_ok('describe')['line']).get('scene')
    check(f'[O6] {label}: describe has the scene file opened', described == scene, described)
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


def camera_of_line(session, line):
    """(state camera 의 위치, heading, pitch 의 text, 줄의 같은 값)"""
    camera = session.ask_ok('state camera')['state']['camera']
    values = parse_line(line)
    return ([str(camera[key]) for key in ['x', 'y', 'z', 'heading', 'pitch']],
            values['camera'].split(',') + [values['heading'], values['pitch']])


def check_restored_scenes(session, check, fresh):
    """[O6]. Sponza 에서 부른다."""
    line = fresh[CORNELL_BOX]['line']
    exported = restored_export(session, line, 'restored-cornell-box-line')
    now, expected = camera_of_line(session, line)
    scene = session.ask_ok('state scene')['state']['scene']['path']
    pixels = differing_pixels(exported, fresh[CORNELL_BOX]['restored'])
    check('[O6] restore in Sponza of a line taken in the Cornell box opens the Cornell box with the camera of the line, and the path tracer '
          'export is bit-identical to restoring it in an app started with the Cornell box', scene == CORNELL_BOX and now == expected and pixels == 0,
          f'scene {scene}, camera {" ".join(now)}, {pixels} pixels differ')
    sponza = fresh[SPONZA]['line']
    old = format_line({key: value for key, value in parse_line(sponza).items() if key != 'scene'})
    for label, restored in [('a Sponza line', sponza), ('a line without scene', old)]:
        session.ask_ok(f'scene open {CORNELL_BOX}')
        reply = session.ask(f'restore {restored}')
        now, expected = camera_of_line(session, restored)
        scene = session.ask_ok('state scene')['state']['scene']['path']
        check(f'[O6] restore in the Cornell box of {label} opens Sponza with the camera of the line', reply['ok'] and scene == SPONZA and now == expected,
              reply.get('error') or f'scene {scene}, camera {" ".join(now)}')


def check_unrestorable(session, check, fresh):
    """[O7]. Cornell box 에서 부른다."""
    sponza = parse_line(fresh[SPONZA]['line'])
    lines = [('a Sponza line without heading', 'heading', format_line({key: value for key, value in sponza.items() if key != 'heading'})),
             ('a Sponza line with light intensity -1', 'light intensity', format_line(sponza | {'light intensity': '-1'})),
             ('a line of a missing scene file', 'scene', format_line(sponza | {'scene': write_unopenable()[0][1]}))]
    wrong = []
    for label, key, line in lines:
        before = without_time_varying(session.ask_ok('state')['state'])
        reply = session.ask(f'restore {line}')
        after = without_time_varying(session.ask_ok('state')['state'])
        if reply['ok'] or key not in reply.get('unread', '').split(', ') or after != before:
            wrong.append(f'{label}: {"ok" if reply["ok"] else reply.get("unread")}{", state changed" if after != before else ""}')
        else:
            print(f'      {label}: {reply["error"]} {reply.get("note", "")}')
    check('[O7] restore of a line of another scene that lacks a key, cannot set one or cannot open its scene file fails, names the key in '
          'unread and changes no state, the scene neither', not wrong, '; '.join(wrong) or f'{len(lines)} lines')


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
            check_restored_scenes(session, check, fresh)
            session.ask_ok(f'scene open {CORNELL_BOX}')
            check_unrestorable(session, check, fresh)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise
    return Path(instance['run_dir'])


def check_saved_scene(check, run_dir):
    """[O9]"""
    saved = (run_dir / 'user.ini').read_text(encoding='utf-8')
    names = [name for name in SCENES + [Path(scene).stem for scene in SCENES] + [f'[{name}]' for name in OBJECT_SETTINGS] if name in saved]
    baseline = WORK_DIR / 'after-switching'
    if baseline.exists():
        shutil.rmtree(baseline)
    baseline.mkdir(parents=True)
    for settings in run_dir.glob('*.ini'):
        shutil.copy2(settings, baseline / settings.name)
    instance = start_instance('Debug', build_first=False, baseline=baseline, app_default_scene=True)
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
    check('[O9] the user.ini of a run that ended in the Cornell box names no scene, light or sky, and the app started with it opens Sponza',
          not names and scene == default_scene() == SPONZA, f'names in user.ini: {names or "none"}; opened {scene}')


def write_object_scenes():
    """[O10], [O11] 의 scene file 을 Cornell box 의 model 과 camera 로 쓰고, 그 MiniEngine/ModelViewer 기준 경로를 (OBJECTS_SCENE,
    NO_OBJECTS_SCENE) 로 돌려준다. OBJECTS_SCENE 은 OBJECT_SETTINGS 를 주석과 함께 CRLF 로, NO_OBJECTS_SCENE 은 물체의 section 없이
    LF 로 쓴다."""
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    cornell = read_scene_file(CORNELL_BOX)
    gltf = Path(os.path.relpath(VIEWER_DIR / Path(CORNELL_BOX).parent / cornell['Model']['glTF'], WORK_DIR)).as_posix()
    camera = ''.join(f'{key}={value}\n' for key, value in cornell['Camera'].items())
    common = f'; [O10], [O11] 의 scene file\n[Model]\nglTF={gltf}\nScale={cornell["Model"]["Scale"]}\n\n[Camera]\n{camera}'
    objects = common + ''.join(f'\n[{name}]\n; 기본값과 다른 값\n' + ''.join(f'{key}={value}\n' for key, value in values.items())
                               for name, values in OBJECT_SETTINGS.items())
    OBJECTS_SCENE.write_bytes(objects.replace('\n', '\r\n').encode('utf-8'))
    NO_OBJECTS_SCENE.write_bytes(common.encode('utf-8'))
    return tuple(scene.relative_to(VIEWER_DIR).as_posix() for scene in (OBJECTS_SCENE, NO_OBJECTS_SCENE))


def object_settings(session):
    return {name: session.ask_ok(f'editor get {name}')['values'] for name in OBJECT_SETTINGS}


def with_object_values(text, settings):
    """text 에서 settings 의 section 에 있는 key 줄의 값만 settings 의 값으로 바꾼 것"""
    newline = '\r\n' if '\r\n' in text else '\n'
    lines, section = [], None
    for line in text.split(newline):
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1]
        elif section in settings and '=' in line and not line.startswith(';'):
            key = line.split('=', 1)[0]
            line = f'{key}={settings[section][key]}'
        lines.append(line)
    return newline.join(lines)


def check_object_sections(session, check, objects_scene, no_objects_scene, defaults):
    """[O10]. no_objects_scene 으로 띄운 app 에서 부른다."""
    session.ask_ok(f'scene open {objects_scene}')
    opened = object_settings(session)
    session.ask_ok(f'light set {LINE_LIGHT}')
    session.ask_ok(f'editor set Sky Light: {LINE_SKY}')
    session.ask_ok(f'scene open {no_objects_scene}')
    without = object_settings(session)
    session.ask_ok(f'light set {LINE_LIGHT}')
    session.ask_ok(f'scene open {objects_scene}')
    again = object_settings(session)
    same_as_defaults = [name for name in OBJECT_SETTINGS if defaults[name] == OBJECT_SETTINGS[name]]
    check('[O10] opening a scene file gives each object the settings of its section, and the defaults without one, and changes not saved '
          'are lost', opened == OBJECT_SETTINGS and without == defaults and again == OBJECT_SETTINGS and not same_as_defaults,
          f'opened {opened}; without sections {without}, defaults {defaults}; opened again {again}')
    missing = session.ask('editor get No Such Object')
    check('[O10] editor get of an object not in the scene fails', not missing['ok'], missing.get('error'))


def check_saved_objects(session, check, objects_scene, no_objects_scene, defaults):
    """[O11]"""
    session.ask_ok(f'scene open {objects_scene}')
    session.ask_ok(f'light set {LINE_LIGHT}')
    session.ask_ok(f'editor set Sky Light: {LINE_SKY}')
    changed = object_settings(session)
    path = VIEWER_DIR / objects_scene
    before = path.read_bytes().decode('utf-8')
    reply = session.ask_ok('scene save')
    after = path.read_bytes().decode('utf-8')
    session.ask_ok(f'scene open {no_objects_scene}')
    session.ask_ok(f'scene open {objects_scene}')
    reopened = object_settings(session)
    expected = with_object_values(before, changed)
    check('[O11] scene save writes the settings of the objects to the key lines of their sections, leaves the comments, the other lines '
          'and the CRLF line ends, and opening the scene file again gives them',
          after == expected and reopened == changed and reply.get('path') == objects_scene,
          f'file {"as expected" if after == expected else "differs: " + repr(after)}; reopened {reopened}, saved {changed}; reply {reply}')

    path = VIEWER_DIR / no_objects_scene
    original = path.read_bytes().decode('utf-8')
    session.ask_ok(f'scene open {no_objects_scene}')
    session.ask_ok('scene save')
    appended = path.read_bytes().decode('utf-8')
    expected = original + ''.join(f'\n[{name}]\n' + ''.join(f'{key}={value}\n' for key, value in values.items())
                                  for name, values in defaults.items())
    check('[O11] scene save of a scene file without the sections of the objects adds them at its end and leaves the lines before',
          appended == expected, repr(appended))


def object_settings_run(check):
    """[O10], [O11]: 물체의 section 이 없는 scene file 로 띄워 그 값을 기본값으로 읽고, 두 scene file 을 오가며 열고 저장한다."""
    objects_scene, no_objects_scene = write_object_scenes()
    instance = start_instance('Debug', scene=no_objects_scene, build_first=False)
    try:
        session = Session(instance)
        try:
            defaults = object_settings(session)
            check_object_sections(session, check, objects_scene, no_objects_scene, defaults)
            check_saved_objects(session, check, objects_scene, no_objects_scene, defaults)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise


def command_check(args):
    check = Checks()
    try:
        fresh = {scene: fresh_run(check, scene, not args.no_build and scene == SCENES[0]) for scene in SCENES}
        started_in_cornell_box_run(check, fresh)
        run_dir = switching_run(check, fresh)
        check_saved_scene(check, run_dir)
        object_settings_run(check)
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
