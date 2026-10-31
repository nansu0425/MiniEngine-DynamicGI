"""Cornell box 를 scene file 로 열어, load 한 scene 과 emission 과 camera 의 되돌림을 확인한다.

  python -I scripts/check-cornell-box.py check   # Debug 로 네 번 띄워 아래 기준을 모두 판정한다

scripts/fetch-assets.ps1 이 받아 바꾼 Cornell box (SCENE) 를 연다. 정답은 SCENE 의 glTF 를 만든 Mitsuba scene (glTF 에
scripts/cornellbox.py 가 남긴 경로) 을 cornellbox.py 로 읽어 해석적으로 구하므로 python -I 로 돌린다. 번호는 작업 13 (a) 의 완료 기준 번호다. app 을 직접 띄우고 끝내며, 이미 띄운 harness
instance 는 먼저 끝낸다. 하나라도 실패하면 exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고
종료 때 LIVE_DEVICE 하나만 남는지도 본다.

판정할 pixel 은 pick-known-answers 처럼 pixel 의 가운데와 네 변의 가운데에서 같은 면을 맞는 pixel 이다. world 와 Mitsuba 좌표는
scene file 의 Scale 로 바꾼다.

  [W2] 처음 화면의 pick grid (GRID) 에서 판정한 칸의 material, 거리, 위치가 Mitsuba scene 의 면으로 구한 값과 같고 (거리와 위치는
       거리의 상대 1e-5), 그 칸을 pick 한 normal 이 면의 normal 과 성분마다 1e-3 안이다. 판정한 칸에 shape 마다 1 칸 이상이 있다.
  [W3] glTF 의 material 이 Mitsuba 의 bsdf 와 같다. baseColorFactor 는 reflectance 를 float32 로 바꾼 값이고 metallic 0, roughness 1,
       twosided bsdf 는 doubleSided 다. light 의 앞면만 emissive 이고 single-sided 이며 그 triangle 의 앞면이 Mitsuba 의 normal 쪽이다.
       app 에서 light 를 위에서 내려다보면 뒷면을 맞고 Max Bounces 0 의 PT 가 0 이다.
  [E1] Max Bounces 0, directional light 와 sky 의 intensity 0, Shading Lambert, Output Radiance 의 PT export 에서, 아홉 sample (가운데, 네 변의 가운데,
       네 모서리) 이 모두 light 의 앞면을 맞는 pixel 이 MIN_LIGHT_PIXELS 이상이고 모두 Mitsuba 의 radiance 와 상대 1e-6 안이다. light 를
       맞는 sample 이 자기와 이웃 pixel 에 하나도 없는 pixel 은 0 이다.
  [E2] KHR_materials_emissive_strength 를 뺀 glTF 에서 [E1] 의 light pixel 은 emissiveFactor 와 상대 1e-6 안이다.
  [N1] [W2] 의 칸에서 shading normal 과 vertex normal 의 각도가 0.1° 이하다.
  [V3] pose, pose look-at, describe, restore 가 error 없이 동작하고 state camera 의 controller 가 editor, state scene 이 SCENE 이다.
  [V4] 처음과 다른 pose, light, sky, PT 설정의 export E1 과 그 줄 L 을 얻는다. app 을 다시 띄워 L 을 restore 하면 state camera 가 L 의
       camera, heading, pitch 와 문자열로 같고, 같은 sample 수의 export E2 는 E1 과 rgba 가 bit 단위로 같다.
  [V5] pose 를 바꾸고 끝낸 run 의 user.ini 의 Editor Camera 에는 Move Speed 만 있다. 예전 형식으로 camera 의 위치와 방향을 적은
       user.ini 로 띄워도 camera 는 scene file 의 pose 로 시작한다.
  [V6] L 은 scene=SCENE 을 담는다. Sponza 의 줄 (scene 을 Sponza 로 바꾼 L 과 scene 을 뺀 L) 을 Cornell box 에서 restore 하면 Sponza 를
       열고, L 을 Sponza 에서 restore 하면 Cornell box 를 연다. 그 뒤 state scene 이 줄의 scene 이고 state camera 가 줄과 문자열로 같다.
  [V7] 처음 화면의 pick grid 에 light, 두 상자, 바닥, 천장, 세 벽이 1 칸 이상씩 있다.
  [V8] scene file 에 적은 처음 pose 가 Mitsuba scene 의 sensor 와 같다. scene file 의 Position 과 띄운 직후 state camera 의
       위치가 sensor 의 to_world 의 이동에 Scale 을 곱한 float32 와 같고, state camera 의 forward 가 to_world 가 옮긴 +z 와 성분마다
       1e-6 안이다.
"""

import argparse
import json
import math
import shutil
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cornellbox
from checks import Checks, stop_and_check
from imagefiles import ImageFileError, read_dds_rgba32f
from pathtracerchecks import BASE, SKY_LIGHT_OBJECT, SKY_RADIANCE, set_path_tracer
from pickanswers import PIXEL_CORNER_OFFSETS, PIXEL_EDGE_OFFSETS, camera_ray, grid_cells, is_unambiguous, nearest_face, vector_of
from viewer import (BASELINE_DIR, HARNESS_DIR, SETTLE_FRAMES, VIEWER_DIR, HarnessError, Session, default_scene, read_scene_file, shots_dir,
                    start_instance, stop_instance)

SCENE = 'Scenes/CornellBox.ini'
NO_STRENGTH_DIR = HARNESS_DIR / 'cornell-box-no-strength'
OLD_BASELINE_DIR = HARNESS_DIR / 'cornell-box-old-user-ini'
# 행이 light 를 지나는 grid. light 는 처음 화면에서 세로로 약 10 pixel 이므로 행의 간격과 위치를 보고 골랐다.
GRID = (81, 50)
SHAPES = ['Light', 'ShortBox', 'TallBox', 'Floor', 'Ceiling', 'BackWall', 'LeftWall', 'RightWall']
MIN_LIGHT_PIXELS = 100
EMISSION_SAMPLES = 16
EMISSION = BASE | {'Shading': 'Lambert', 'Output': 'Radiance', 'Max Bounces': 0, SKY_RADIANCE: 0, 'Target Samples': EMISSION_SAMPLES}
# light 와 천장 사이에서 light 를 내려다보는 pose. camera_ray 가 쓰는 오른쪽 방향이 정해지도록 수직에서 조금 기울인다.
ABOVE_LIGHT_PITCH = -1.5
# [V4] 의 줄. 처음과 모든 조건이 다르다.
LINE = {
    'pose': '150 400 2500 0.2 0.05',
    'light': 'elevation=25 azimuth=80 color=1,0.9,0.8 intensity=6',
    'sky': 'Color=0.5,0.75,1; Intensity=0.25',
    'path tracer': {'Shading': 'glTF', 'Output': 'Radiance', 'Seed': 5, 'Max Bounces': 6, 'Russian Roulette': 1, 'Geometric Normals': 0},
    'samples': 16,
}
WAIT_TIMEOUT = 900


def parse_line(line):
    return dict(item.split('=', 1) for item in line.split('; '))


def format_line(values):
    return '; '.join(f'{key}={value}' for key, value in values.items())


class Answers:
    """Mitsuba scene 으로 구한, 지금의 camera 에서 sample 이 맞는 면"""

    def __init__(self, mitsuba, scale, state):
        self.faces = cornellbox.world_faces(mitsuba, scale)
        self.camera, self.scene = state['camera'], state['ui']['scene']
        self.forward = vector_of(self.camera['forward'])

    def expect(self, sample):
        origin, direction = camera_ray(self.camera, self.scene, sample)
        cosine = direction @ self.forward
        return origin, direction, nearest_face(origin, direction, self.faces, float(self.camera['near']) / cosine,
                                               float(self.camera['far']) / cosine)

    def seen_at(self, sample):
        found = self.expect(sample)[2]
        return None if found is None else (found[1].material, tuple(found[1].normal))

    def unambiguous(self, pixel, offsets=PIXEL_EDGE_OFFSETS):
        return is_unambiguous(self.seen_at, pixel, offsets)

    def to_pixel(self, point):
        """world 의 점이 보이는 client pixel 좌표 (camera_ray 의 반대)"""
        origin = vector_of(self.camera)
        right = np.cross(self.forward, [0.0, 1.0, 0.0])
        right /= np.linalg.norm(right)
        up = np.cross(right, self.forward)
        half_height = math.tan(float(self.camera['vertical_fov']) / 2)
        half_width = half_height * self.scene['width'] / self.scene['height']
        offset = np.asarray(point) - origin
        depth = offset @ self.forward
        x, y = offset @ right / (depth * half_width), offset @ up / (depth * half_height)
        return (self.scene['left'] + 0.5 + (x + 1) * self.scene['width'] / 2, self.scene['top'] + 0.5 + (1 - y) * self.scene['height'] / 2)


def scene_scale():
    return float(read_scene_file(SCENE)['Model']['Scale'])


def light_material(answers, emissive=True):
    return next(face.material for face in answers.faces if face.shape == 'Light' and face.emissive == emissive)


def check_scene_camera(check, mitsuba, scale, camera):
    """[V8]"""
    written = np.float32([float(value) for value in read_scene_file(SCENE)['Camera']['Position'].split(',')])
    started = np.float32([float(camera[axis]) for axis in 'xyz'])
    expected = np.float32(mitsuba.sensor_to_world[:3, 3] * scale)
    direction = mitsuba.sensor_to_world[:3, :3] @ np.array([0.0, 0.0, 1.0])
    direction /= np.linalg.norm(direction)
    forward = vector_of(camera['forward'])
    check('[V8] the scene file pose is the Mitsuba sensor: its position times Scale as float32, and its +z as the forward',
          np.array_equal(written, expected) and np.array_equal(started, expected) and np.abs(forward - direction).max() <= 1e-6,
          f'scene file {written.tolist()}, state {started.tolist()}, sensor {expected.tolist()}; forward {forward.tolist()}, sensor +z {direction.tolist()}')


def check_grid(session, check, answers):
    """[V7], [W2], [N1]"""
    grid = grid_cells(session.ask_ok(f'pick grid {GRID[0]}x{GRID[1]}')['grid'])
    shape_of = {face.material: face.shape for face in answers.faces}
    seen = {shape_of.get(int(values['material'])) for _, _, values in grid if values['material'] is not None}
    missing = [shape for shape in SHAPES if shape not in seen]
    check('[V7] the first view shows the light, two boxes, floor, ceiling and three walls in pick grid cells', not missing,
          f'missing {missing}' if missing else ', '.join(SHAPES))

    wrong, judged, angles = [], {}, {}
    for x, y, values in grid:
        if not answers.unambiguous((x, y)):
            continue
        origin, direction, found = answers.expect(np.array([x, y]) + 0.5)
        if found is None:
            judged['sky'] = judged.get('sky', 0) + 1
            if values['material'] is not None:
                wrong.append(f'{x},{y}: {values} where nothing is')
            continue
        t, face = found
        judged[face.shape] = judged.get(face.shape, 0) + 1
        position = None if values['position'] is None else np.array([float(value) for value in values['position'].split(',')])
        if values['material'] != str(face.material) or position is None or abs(float(values['distance']) - t) > 1e-5 * t or \
                np.linalg.norm(position - (origin + t * direction)) > 1e-5 * t:
            wrong.append(f'{x},{y}: {values} where {face.shape} is at {t}')
            continue
        pick = session.ask_ok(f'pick {x} {y}')['pick']
        toward_camera = face.normal if direction @ face.normal < 0 else -face.normal
        if np.abs(vector_of(pick['normal']) - toward_camera).max() > 1e-3:
            wrong.append(f'{x},{y}: normal {pick["normal"]} where {face.shape} has {toward_camera.tolist()}')
        shading, vertex = vector_of(pick['shading_normal']), vector_of(pick['vertex_normal'])
        # acos 는 1 에 가까운 cos 에서 float 의 길이 오차를 각도로 키우므로 sin 과 cos 로 구한다.
        angle = math.degrees(math.atan2(np.linalg.norm(np.cross(shading, vertex)), shading @ vertex))
        angles[face.shape] = max(angles.get(face.shape, 0.0), angle)
    lacking = [shape for shape in SHAPES if judged.get(shape, 0) == 0]
    check('[W2] pick grid finds the material, distance and position of the Mitsuba scene in each judged cell, and pick its normal',
          not wrong and not lacking, '; '.join(wrong[:3]) + (f'; no judged cell on {lacking}' if lacking else '') or f'judged cells {judged}')
    largest = max(angles.values(), default=None)
    check('[N1] the shading normal is within 0.1 degrees of the vertex normal in the judged cells', largest is not None and largest <= 0.1,
          ', '.join(f'{shape} {angle:.2e}' for shape, angle in sorted(angles.items())) + ' degrees at most')


def light_pixels(answers, light):
    """(sample 아홉이 모두 light 의 앞면을 맞는 pixel, light 를 맞는 sample 이 자기와 이웃 pixel 에 하나도 없는 pixel) 의 mask"""
    scene = answers.scene
    shape = (scene['height'], scene['width'])
    inside, touched = np.zeros(shape, dtype=bool), np.zeros(shape, dtype=bool)
    corners = [face for face in answers.faces if face.material == light]
    points = [face.center + su * face.half_u + sv * face.half_v for face in corners for su in (-1, 1) for sv in (-1, 1)]
    projected = np.array([answers.to_pixel(point) for point in points]) - [scene['left'], scene['top']]
    low, high = np.floor(projected.min(axis=0)).astype(int) - 2, np.ceil(projected.max(axis=0)).astype(int) + 2
    offsets = PIXEL_EDGE_OFFSETS + PIXEL_CORNER_OFFSETS
    for y in range(max(low[1], 0), min(high[1], shape[0])):
        for x in range(max(low[0], 0), min(high[0], shape[1])):
            pixel = (scene['left'] + x, scene['top'] + y)
            hits = [(answers.seen_at(np.array(pixel) + 0.5 + offset) or (None,))[0] == light for offset in offsets]
            inside[y, x] = all(hits)
            touched[y, x] = any(hits)
    near = touched.copy()
    near[1:, :] |= touched[:-1, :]
    near[:-1, :] |= touched[1:, :]
    near[:, 1:] |= near[:, :-1].copy()
    near[:, :-1] |= near[:, 1:].copy()
    return inside, ~near


def export_emission(session, name):
    session.ask_ok('light set intensity=0')
    set_path_tracer(session, EMISSION)
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={EMISSION_SAMPLES}', timeout=WAIT_TIMEOUT)
    return read_dds_rgba32f(session.ask_ok(f'capture pt {shots_dir(session.instance) / name}')['path'])


def check_emission(session, check, answers, expected, label):
    """[E1] 이나 [E2]. expected 는 light pixel 의 rgb 다."""
    image = export_emission(session, label)
    inside, dark = light_pixels(answers, light_material(answers))
    rgb = image[..., :3].astype(np.float64)
    expected = np.array(expected, dtype=np.float64)
    error = np.abs(rgb[inside] - expected) / expected
    lit = rgb[dark]
    check(f'[{label}] every pixel all of whose nine samples hit the light front is {expected.tolist()} within 1e-6, and there are '
          f'{MIN_LIGHT_PIXELS} or more', inside.sum() >= MIN_LIGHT_PIXELS and error.size > 0 and error.max() <= 1e-6,
          f'{int(inside.sum())} pixels, largest relative error {error.max() if error.size else None:.3g}')
    check(f'[{label}] every pixel with no sample on the light in it or its neighbors is 0', not np.any(lit != 0.0),
          f'{int(np.count_nonzero(np.any(lit != 0.0, axis=1)))} of {len(lit)} pixels are not 0')
    return image


def check_light_back(session, check, answers, scale):
    """[W3] 의 app 쪽: light 와 천장 사이에서 내려다보면 light 의 뒷면을 맞고 emission 이 없다."""
    light = [face for face in answers.faces if face.shape == 'Light' and face.emissive][0]
    ceiling = [face for face in answers.faces if face.shape == 'Ceiling'][0]
    height = (light.center[1] + ceiling.center[1]) / 2
    session.ask_ok(f'pose {float(light.center[0])!r} {float(height)!r} {float(light.center[2])!r} 0 {ABOVE_LIGHT_PITCH}')
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={EMISSION_SAMPLES}', timeout=WAIT_TIMEOUT)
    image = read_dds_rgba32f(session.ask_ok(f'capture pt {shots_dir(session.instance) / "above-light"}')['path'])
    scene = session.ask_ok('state ui')['state']['ui']['scene']
    center = (scene['left'] + scene['width'] // 2, scene['top'] + scene['height'] // 2)
    pick = session.ask_ok(f'pick {center[0]} {center[1]}')['pick']
    block = image[scene['height'] // 2 - 50:scene['height'] // 2 + 50, scene['width'] // 2 - 50:scene['width'] // 2 + 50, :3]
    back = light_material(answers, emissive=False)
    check('[W3] looking down at the light from between it and the ceiling hits its back, which does not glow',
          pick['hit'] and pick['material'] == back and pick['front_face'] and not np.any(block != 0.0),
          f'material {pick.get("material")} (back {back}), front face {pick.get("front_face")}, '
          f'{int(np.count_nonzero(np.any(block != 0.0, axis=2)))} of 100x100 center pixels not 0')


def converted_gltf():
    return cornellbox.written_gltf(VIEWER_DIR / SCENE)


def check_materials(check, mitsuba):
    """[W3] 의 glTF 쪽"""
    gltf = json.loads(converted_gltf().read_text(encoding='utf-8'))
    wrong = []
    names = [material['name'] for material in gltf['materials']]
    for shape in mitsuba.shapes:
        if shape.id not in names:
            wrong.append(f'no material {shape.id}')
            continue
        material = gltf['materials'][names.index(shape.id)]
        pbr = material['pbrMetallicRoughness']
        if np.float32(pbr['baseColorFactor'][:3]).tolist() != np.float32(shape.reflectance).tolist() or pbr['metallicFactor'] != 0 or \
                pbr['roughnessFactor'] != 1:
            wrong.append(f'{shape.id}: {pbr}')
        emissive = shape.radiance is not None
        if material.get('doubleSided', False) == emissive or ('emissiveFactor' in material) != emissive:
            wrong.append(f'{shape.id}: doubleSided {material.get("doubleSided")}, emissiveFactor {material.get("emissiveFactor")}')
        if emissive:
            strength = material.get('extensions', {}).get(cornellbox.EMISSIVE_STRENGTH, {}).get('emissiveStrength', 1.0)
            if not np.allclose(np.array(material['emissiveFactor']) * strength, shape.radiance, rtol=1e-12):
                wrong.append(f'{shape.id}: emission {material["emissiveFactor"]} x {strength}')
            wrong += light_winding_problems(gltf, names.index(shape.id), shape)
    check('[W3] each glTF material has the reflectance of its Mitsuba bsdf as baseColorFactor, metallic 0, roughness 1, doubleSided but '
          'the light front, which is the only emissive one and faces the Mitsuba normal', not wrong, '; '.join(wrong) or f'{len(names)} materials')


def light_winding_problems(gltf, material, shape):
    """light 의 앞면 primitive 의 triangle 이 counter-clockwise 로 보이는 쪽이 Mitsuba 의 normal 쪽인가"""
    data = (converted_gltf().parent / gltf['buffers'][0]['uri']).read_bytes()

    def read(accessor, dtype, components):
        accessor = gltf['accessors'][accessor]
        view = gltf['bufferViews'][accessor['bufferView']]
        return np.frombuffer(data, dtype=dtype, count=accessor['count'] * components, offset=view['byteOffset'])

    problems = []
    normal = shape.faces[0][3]
    for mesh in gltf['meshes']:
        for primitive in mesh['primitives']:
            if primitive['material'] != material:
                continue
            positions = read(primitive['attributes']['POSITION'], np.float32, 3).reshape(-1, 3).astype(np.float64)
            for a, b, c in read(primitive['indices'], np.uint16, 1).reshape(-1, 3):
                if np.cross(positions[b] - positions[a], positions[c] - positions[a]) @ normal <= 0.0:
                    problems.append(f'{shape.id}: a front triangle faces away from the Mitsuba normal')
    return problems


def set_line_conditions(session):
    session.ask_ok(f'pose {LINE["pose"]}')
    session.ask_ok(f'light set {LINE["light"]}')
    session.ask_ok(f'editor set {SKY_LIGHT_OBJECT}: {LINE["sky"]}')
    settings = LINE['path tracer'] | {'Show': 1, 'Samples per Frame': 1, 'Target Samples': LINE['samples']}
    session.ask_ok('settings set Engine Tuning: ' + '; '.join(f'Path Tracer/{key}={value}' for key, value in settings.items()))
    session.ask_ok('pt reset')
    session.ask_ok(f'pt wait samples={LINE["samples"]}', timeout=WAIT_TIMEOUT)
    reply = session.ask_ok(f'capture pt {shots_dir(session.instance) / "line"}')
    return reply['line'], read_dds_rgba32f(reply['path'])


def check_restored_scene(session, check, line, scene, label):
    reply = session.ask(f'restore {line}')
    state = session.ask_ok('state')['state']
    values = parse_line(line)
    camera = [str(state['camera'][key]) for key in ['x', 'y', 'z', 'heading', 'pitch']]
    check(f'[V6] restore of {label} opens {scene} and gives the camera of the line',
          reply['ok'] and state['scene']['path'] == scene and camera == values['camera'].split(',') + [values['heading'], values['pitch']],
          reply.get('error') or f'scene {state["scene"]["path"]}, camera {" ".join(camera)}')


def check_commands(session, check):
    """[V3]"""
    state = session.ask_ok('state')['state']
    pose = session.ask_ok('pose')
    replies = [session.ask(f'pose {float(pose["x"]) + 10!r} {pose["y"]} {pose["z"]} {pose["heading"]} {pose["pitch"]}'),
               session.ask('pose look-at 0 512 0')]
    line = session.ask('describe')
    replies += [line, session.ask(f'restore {line.get("line", "")}')]
    failed = [reply.get('error') for reply in replies if not reply['ok']]
    check('[V3] pose, pose look-at, describe and restore work, state camera is the editor camera and state scene is the scene file',
          not failed and state['camera']['controller'] == 'editor' and state['scene']['path'] == SCENE,
          f'{failed}; controller {state["camera"]["controller"]}; scene {state["scene"]}')


def first_run(check, no_build, mitsuba, scale):
    """처음 화면과 emission 을 판정하고 [V4] 의 (줄, export) 를 돌려준다."""
    instance = start_instance('Debug', scene=SCENE, build_first=not no_build)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            answers = Answers(mitsuba, scale, session.ask_ok('state')['state'])
            check_scene_camera(check, mitsuba, scale, answers.camera)
            check_grid(session, check, answers)
            light = next(shape for shape in mitsuba.shapes if shape.radiance is not None)
            check_emission(session, check, answers, light.radiance, 'E1')
            check_light_back(session, check, answers, scale)
            check_commands(session, check)
            line, image = set_line_conditions(session)
            check('[V6] the line has the scene file', parse_line(line).get('scene') == SCENE, line)
            check_restored_scene(session, check, format_line(parse_line(line) | {'scene': default_scene()}), default_scene(),
                                 'the line with the Sponza scene')
            session.ask_ok(f'scene open {SCENE}')
            check_restored_scene(session, check, format_line({key: value for key, value in parse_line(line).items() if key != 'scene'}),
                                 default_scene(), 'the line without scene, which is of Sponza')
        finally:
            session.close()
        stopped = stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise
    check_saved_camera(check, Path(instance['run_dir']) / 'user.ini')
    return line, image, stopped


def check_saved_camera(check, path):
    """[V5] 의 앞: 끝낸 run 의 user.ini 에는 camera 의 이동 속도만 남는다."""
    import configparser
    ini = configparser.ConfigParser(delimiters=('=',), interpolation=None)
    ini.optionxform = str
    ini.read(path, encoding='utf-8')
    keys = list(ini['Editor Camera']) if ini.has_section('Editor Camera') else None
    check('[V5] the user.ini a run left after moving the camera has only Move Speed in Editor Camera', keys == ['Move Speed'], keys)


def write_old_baseline():
    """harness baseline 에 예전 형식의 camera 위치와 방향을 더한 폴더"""
    if OLD_BASELINE_DIR.exists():
        shutil.rmtree(OLD_BASELINE_DIR)
    shutil.copytree(BASELINE_DIR, OLD_BASELINE_DIR)
    settings = OLD_BASELINE_DIR / 'user.ini'
    old = 'Heading=1.60599577\nPitch=-0.534952283\nPosition X=1098.72424\nPosition Y=651.495239\nPosition Z=-38.6904297\n'
    text = settings.read_text(encoding='utf-8')
    settings.write_text(text.replace('[Editor Camera]\n', '[Editor Camera]\n' + old, 1), encoding='utf-8')
    return OLD_BASELINE_DIR


def second_run(check, line, image):
    """[V5] 의 뒤와 [V4]"""
    instance = start_instance('Debug', scene=SCENE, build_first=False, baseline=write_old_baseline())
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            camera = session.ask_ok('state camera')['state']['camera']
            start = read_scene_file(SCENE)['Camera']
            expected = [np.float32(value) for value in start['Position'].split(',') + [start['Heading'], start['Pitch']]]
            got = [np.float32(camera[key]) for key in ['x', 'y', 'z', 'heading', 'pitch']]
            check('[V5] with an old user.ini that has the camera position and direction, the camera starts at the scene file pose',
                  got == expected, f'{got} vs {expected}')

            session.ask_ok(f'restore {line}')
            camera = session.ask_ok('state camera')['state']['camera']
            values = parse_line(line)
            restored = [str(camera[key]) for key in ['x', 'y', 'z']] + [str(camera['heading']), str(camera['pitch'])]
            check('[V4] after a restart, restore of the line gives its camera in state camera as the same text',
                  restored == values['camera'].split(',') + [values['heading'], values['pitch']], ' '.join(restored))
            session.ask_ok('settings set Engine Tuning: Path Tracer/Show=1')
            session.ask_ok(f'pt wait samples={LINE["samples"]}', timeout=WAIT_TIMEOUT)
            again = read_dds_rgba32f(session.ask_ok(f'capture pt {shots_dir(session.instance) / "line-restarted"}')['path'])
            differing = int(np.count_nonzero(np.any(again.view(np.uint32) != image.view(np.uint32), axis=2))) if again.shape == image.shape else None
            check(f'[V4] the export after the restart is bit-identical to the one before, at {LINE["samples"]} samples of Shading glTF',
                  differing == 0, f'{differing} pixels differ' if differing is not None else f'shape {again.shape} vs {image.shape}')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise


def sponza_run(check, line):
    """[V6] 의 Sponza 쪽"""
    instance = start_instance('Debug', build_first=False)
    try:
        session = Session(instance)
        try:
            check_restored_scene(session, check, line, SCENE, 'the Cornell box line in Sponza')
        finally:
            session.close()
        stop_and_check(instance, check)
    except HarnessError:
        stop_instance(instance, kill_now=True)
        raise


def write_without_strength():
    """KHR_materials_emissive_strength 를 뺀 glTF 와 그것을 SCENE 처럼 여는 scene file. scene file 의 경로를 돌려준다."""
    NO_STRENGTH_DIR.mkdir(parents=True, exist_ok=True)
    source = converted_gltf()
    gltf = json.loads(source.read_text(encoding='utf-8'))
    gltf.pop('extensionsUsed', None)
    for material in gltf['materials']:
        material.pop('extensions', None)
    (NO_STRENGTH_DIR / source.name).write_text(json.dumps(gltf, indent=2), encoding='utf-8')
    shutil.copy2(source.parent / gltf['buffers'][0]['uri'], NO_STRENGTH_DIR / gltf['buffers'][0]['uri'])
    scene = read_scene_file(SCENE)
    camera = scene['Camera']
    (NO_STRENGTH_DIR / 'CornellBox.ini').write_text(
        f'[Model]\nglTF={source.name}\nScale={scene["Model"]["Scale"]}\n\n'
        f'[Camera]\nPosition={camera["Position"]}\nHeading={camera["Heading"]}\nPitch={camera["Pitch"]}\n', encoding='utf-8')
    return (NO_STRENGTH_DIR / 'CornellBox.ini').relative_to(VIEWER_DIR).as_posix(), gltf


def no_strength_run(check, mitsuba, scale):
    """[E2]"""
    scene, gltf = write_without_strength()
    instance = start_instance('Debug', scene=scene, build_first=False)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            session.ask_ok(f'frames {SETTLE_FRAMES}')
            answers = Answers(mitsuba, scale, session.ask_ok('state')['state'])
            factor = gltf['materials'][light_material(answers)]['emissiveFactor']
            check_emission(session, check, answers, np.float32(factor), 'E2')
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError):
        stop_instance(instance, kill_now=True)
        raise


def command_check(args):
    check = Checks()
    try:
        mitsuba = cornellbox.read(cornellbox.written_source(converted_gltf()))
        scale = scene_scale()
        check('the scene scale is a power of two', math.frexp(scale)[0] == 0.5, scale)
        check_materials(check, mitsuba)
        line, image, _ = first_run(check, args.no_build, mitsuba, scale)
        second_run(check, line, image)
        sponza_run(check, line)
        no_strength_run(check, mitsuba, scale)
    except (HarnessError, ImageFileError, cornellbox.SceneError) as error:
        check('check ran to the end', False, str(error))
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('check', help='start Debug four times and judge the criteria')
    command.add_argument('--no-build', action='store_true')
    command.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
