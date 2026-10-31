"""Benedikt Bitterli 의 Cornell Box (Mitsuba 3 scene) 를 glTF 로 바꾼다.

  python -I scripts/cornellbox.py <Mitsuba scene> <scene file>

scripts/fetch-assets.ps1 이 zip 을 받아 푼 뒤에 부른다. glTF 는 그 glTF 를 여는 ModelViewer 의 scene file 이 [Model] glTF 로
가리키는 곳에 쓰고, glTF 의 asset.extras.source 에 Mitsuba scene 의 경로를 glTF 폴더 기준으로 남긴다 (SOURCE_KEY).
scripts/check-cornell-box.py 가 그 경로로 정답을 구한다. 받은 파일을 읽으므로 python -I 로 돌리고, 자기 폴더만 sys.path 에 넣어
module 을 import 한다. 수치는 모두 scene 에서 읽고, 여기에는 Mitsuba 의 shape 정의만 둔다. scene 이 이 script 가 아는 것 밖의
plugin 이나 transform 을 쓰면 바꾸지 않고 실패한다.

glTF 좌표는 Mitsuba 의 world 좌표 그대로이고 축을 바꾸지 않는다. 바꾸는 규칙은 아래와 같다.

- shape 마다 mesh 하나와 node 하나를 두고, to_world 를 vertex 에 미리 곱해 node 에는 transform 을 두지 않는다.
- rectangle 은 local 의 xy 평면에서 [-1, 1]^2 를 덮고 normal 이 +z 인 면이고, cube 는 [-1, 1]^3 의 상자이며 면의 normal 이
  바깥을 향한다 (Mitsuba 3 문서의 Rectangle 과 Cube 절. references/ 에 받지 않았다). normal 은 Mitsuba 처럼 to_world 의 inverse transpose 로 옮긴다. glTF 의 앞면이
  그 normal 쪽이 되도록 triangle 을 counter-clockwise 로 둔다. UV 는 면마다 [0, 1]^2 다.
- twosided 안의 diffuse bsdf 는 baseColorFactor 가 reflectance 이고 metallic 0, roughness 1 인 doubleSided material 이다. glTF 의
  metallic-roughness 는 Lambert 가 아니므로 Mitsuba 와 같은 반사는 path tracer 의 Shading Lambert 로 그릴 때다
  (PathTracerCS.hlsl 의 LAMBERT_SHADING). twosided 가 아닌 bsdf 는 받지 않는다. Mitsuba 에서 그 뒷면은 빛을 받지 않으면서 ray 를
  막는데, 이 repo 는 single-sided 의 뒷면을 없는 것으로 친다 (docs/policy.md 의 "camera 밖에서 본 doubleSided").
- area emitter 는 normal 쪽으로만 빛난다. 그래서 emitter 가 붙은 shape 는 single-sided 인 앞면과, 같은 reflectance 로 반대쪽을
  향하는 single-sided 인 뒷면 두 primitive 로 나눈다. 두 면은 어느 쪽에서 오는 ray 든 하나만 맞으므로 twosided bsdf 처럼 ray 를
  막는다. emission 은 앞면의 material 에만 둔다. core 의 emissiveFactor 는 [0, 1] 이므로 radiance 를 그 최댓값으로 나눈 값과
  KHR_materials_emissive_strength 의 emissiveStrength 로 나눠 담는다.
"""

import json
import os
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gltfwriter import GLTF_FLOAT, GLTF_UNSIGNED_SHORT, BufferBuilder, flatten
from pickanswers import parallelogram
from viewer import VIEWER_DIR, scene_gltf

SOURCE_KEY = 'source'
EMISSIVE_STRENGTH = 'KHR_materials_emissive_strength'

# Mitsuba 3 의 shape 의 local 면. 면마다 (중심, 반 변 u, 반 변 v, 바깥 normal) 이다.
RECTANGLE_FACES = [((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))]


def cube_faces():
    faces = []
    for axis in range(3):
        for sign in (1.0, -1.0):
            center, u, v = [0.0] * 3, [0.0] * 3, [0.0] * 3
            center[axis] = sign
            u[(axis + 1) % 3] = 1.0
            v[(axis + 2) % 3] = 1.0
            faces.append((tuple(center), tuple(u), tuple(v), tuple(center)))
    return faces


SHAPE_FACES = {'rectangle': RECTANGLE_FACES, 'cube': cube_faces()}


class SceneError(Exception):
    pass


def numbers(text, count):
    values = [float(value) for value in text.replace(',', ' ').split()]
    if len(values) != count:
        raise SceneError(f'{text!r} is not {count} numbers')
    return values


def to_world(element):
    """element 의 to_world. matrix 하나만 받는다."""
    import numpy as np
    transforms = element.findall('transform')
    if len(transforms) != 1 or transforms[0].get('name') != 'to_world' or [child.tag for child in transforms[0]] != ['matrix']:
        raise SceneError(f'{element.get("id")}: only a to_world with one matrix is supported')
    return np.array(numbers(transforms[0][0].get('value'), 16)).reshape(4, 4)


def rgb(element, name):
    found = [child for child in element if child.tag == 'rgb' and child.get('name') == name]
    if len(found) != 1:
        raise SceneError(f'{element.get("id") or element.get("type")}: needs one rgb {name}')
    return numbers(found[0].get('value'), 3)


def read_reflectance(element):
    """twosided 안의 diffuse bsdf 의 reflectance"""
    inner = element.findall('bsdf')
    if element.get('type') != 'twosided' or len(inner) != 1 or inner[0].get('type') != 'diffuse':
        raise SceneError(f'bsdf {element.get("id")}: only a diffuse inside twosided is supported')
    return rgb(inner[0], 'reflectance')


def world_face(matrix, face):
    """local 면을 to_world 로 옮긴 (중심, 반 변 u, 반 변 v, 단위 normal). u x v 가 normal 쪽이다."""
    import numpy as np
    linear = matrix[:3, :3]
    center = linear @ np.array(face[0]) + matrix[:3, 3]
    u, v = linear @ np.array(face[1]), linear @ np.array(face[2])
    normal = np.linalg.inv(linear).T @ np.array(face[3])
    normal /= np.linalg.norm(normal)
    if np.cross(u, v) @ normal < 0.0:
        u, v = v, u
    return center, u, v, normal


def read(path):
    """scene 의 shape 와 sensor. shape 마다 id, faces (world 의 면), reflectance, radiance (emitter 가 없으면 None) 를 둔다."""
    root = ElementTree.parse(path).getroot()
    if not root.get('version', '').startswith('3.'):
        raise SceneError(f'{path} is not a Mitsuba 3 scene')
    bsdfs = {element.get('id'): read_reflectance(element) for element in root.findall('bsdf')}
    shapes = []
    for element in root.findall('shape'):
        kind = element.get('type')
        if kind not in SHAPE_FACES:
            raise SceneError(f'shape {element.get("id")}: type {kind} is not supported')
        refs = element.findall('ref')
        if len(refs) != 1 or refs[0].get('id') not in bsdfs or element.findall('bsdf'):
            raise SceneError(f'shape {element.get("id")}: needs one ref to a bsdf')
        emitters = element.findall('emitter')
        if len(emitters) > 1 or (emitters and emitters[0].get('type') != 'area'):
            raise SceneError(f'shape {element.get("id")}: only one area emitter is supported')
        matrix = to_world(element)
        shapes.append(SimpleNamespace(id=element.get('id'), faces=[world_face(matrix, face) for face in SHAPE_FACES[kind]],
                                      reflectance=bsdfs[refs[0].get('id')], radiance=rgb(emitters[0], 'radiance') if emitters else None))
    sensors = root.findall('sensor')
    if len(sensors) != 1 or sensors[0].get('type') != 'perspective':
        raise SceneError('needs one perspective sensor')
    return SimpleNamespace(shapes=shapes, sensor_to_world=to_world(sensors[0]))


def primitives(shape):
    """shape 를 glTF primitive 로 나눈 (material 이름, material, 면들) 의 목록. 면은 앞면이 normal 쪽이다."""
    material = {'pbrMetallicRoughness': {'baseColorFactor': [*shape.reflectance, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 1.0}}
    if shape.radiance is None:
        return [(shape.id, material | {'doubleSided': True}, shape.faces)]
    strength = max(shape.radiance)
    front = material | {'emissiveFactor': [value / strength for value in shape.radiance]}
    if strength > 1.0:
        front['extensions'] = {EMISSIVE_STRENGTH: {'emissiveStrength': strength}}
    back = [(center, v, u, -normal) for center, u, v, normal in shape.faces]
    return [(shape.id, front, shape.faces), (f'{shape.id} back', material, back)]


def write(scene, source, gltf_path):
    """source 에서 읽은 scene 을 gltf_path 에, buffer 를 그 옆의 같은 이름 .bin 에 쓴다. 내용이 같으면 파일을 다시 쓰지 않아 .mini 를
    다시 만들지 않게 한다."""
    import numpy as np
    builder = BufferBuilder()
    meshes, materials, nodes = [], [], []
    for shape in scene.shapes:
        mesh = {'name': shape.id, 'primitives': []}
        for name, material, faces in primitives(shape):
            positions, normals, uvs, indices = [], [], [], []
            for center, u, v, normal in faces:
                first = len(positions)
                for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    positions.append(tuple(float(value) for value in np.float32(center + su * u + sv * v)))
                    normals.append(tuple(float(value) for value in np.float32(normal)))
                    uvs.append(((su + 1) / 2, (sv + 1) / 2))
                indices += [first, first + 1, first + 2, first, first + 2, first + 3]
            count = len(positions)
            mesh['primitives'].append({'attributes': {
                'POSITION': builder.add(flatten(positions), GLTF_FLOAT, 'VEC3', count, with_bounds=True),
                'NORMAL': builder.add(flatten(normals), GLTF_FLOAT, 'VEC3', count),
                'TEXCOORD_0': builder.add(flatten(uvs), GLTF_FLOAT, 'VEC2', count),
            }, 'indices': builder.add(indices, GLTF_UNSIGNED_SHORT, 'SCALAR', len(indices)), 'material': len(materials)})
            materials.append({'name': name, **material})
        meshes.append(mesh)
        nodes.append({'name': shape.id, 'mesh': len(meshes) - 1})

    gltf_path = Path(gltf_path).resolve()
    bin_name = gltf_path.with_suffix('.bin').name
    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/cornellbox.py',
                  'extras': {SOURCE_KEY: Path(os.path.relpath(Path(source).resolve(), gltf_path.parent)).as_posix()}},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'buffers': [{'uri': bin_name, 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    if any(EMISSIVE_STRENGTH in material.get('extensions', {}) for material in materials):
        gltf['extensionsUsed'] = [EMISSIVE_STRENGTH]
    gltf_path.parent.mkdir(parents=True, exist_ok=True)
    for path, data in [(gltf_path.with_name(bin_name), bytes(builder.data)), (gltf_path, json.dumps(gltf, indent=2).encode('utf-8'))]:
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)


def world_faces(scene, scale):
    """glTF 를 scale 배로 연 world 의 면. pick 처럼 single-sided 의 뒷면은 보이지 않는다. 면마다 shape 와 material 의 순서를 둔다."""
    faces, material = [], 0
    for shape in scene.shapes:
        for name, gltf_material, shape_faces in primitives(shape):
            for center, u, v, normal in shape_faces:
                faces.append(parallelogram(scale * center, scale * u, scale * v, normal, gltf_material.get('doubleSided', False),
                                           shape=shape.id, material=material, emissive='emissiveFactor' in gltf_material))
            material += 1
    return faces


def written_gltf(scene_file):
    """scene file (경로) 이 여는 glTF 의 경로"""
    return VIEWER_DIR / scene_gltf(Path(os.path.relpath(Path(scene_file).resolve(), VIEWER_DIR)).as_posix())


def written_source(gltf_path):
    """write 가 gltf_path 에 남긴 Mitsuba scene 의 경로"""
    gltf = json.loads(Path(gltf_path).read_text(encoding='utf-8'))
    return (Path(gltf_path).parent / gltf['asset']['extras'][SOURCE_KEY]).resolve()


def main():
    if len(sys.argv) != 3:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    try:
        gltf_path = written_gltf(sys.argv[2])
        write(read(sys.argv[1]), sys.argv[1], gltf_path)
        print(f'glTF: {gltf_path}')
    except (SceneError, ElementTree.ParseError, ValueError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
