"""pick 명령의 결과를 견줄, 정답을 아는 test scene.

selftest 가 이 scene 을 -model 로 띄우고, pick 이 돌려준 거리, 위치, normal, material 을 여기서 해석적으로 구한 정답과 견준다.
scene 은 xy 평면의 벽 하나와 그 앞의 상자 하나이고, 모든 면이 single-sided 로 바깥을 향한다. -model 의 OrbitCamera 는 입력이
없으면 model 중심의 +z 쪽에서 -z 를 보므로 (OrbitCamera::Update), 벽은 앞면이, 상자는 앞면과 camera 쪽 옆면이 보이고,
벽의 위아래와 양옆으로는 아무것도 없다.

ModelViewer 는 -model 로 연 model 을 ModelInstance::Resize 로 늘리기만 하므로 world 위치는 glTF 위치를 한 배율로 늘리고
옮긴 것이다. 그 배율과 이동은 state 의 model bounds 를 glTF 의 bounds 와 견줘 구한다 (world_transform).
"""

import json
from pathlib import Path
from types import SimpleNamespace

from gltfwriter import GLTF_FLOAT, GLTF_UNSIGNED_SHORT, BufferBuilder, flatten

GLTF_NAME = 'PickKnownAnswers.gltf'

# 면은 중심, 바깥을 향한 normal, 면 위의 한 축 u 와 두 축의 반 길이로 정한다. 다른 축 v 는 cross(normal, u) 라서
# (-u, -v), (u, -v), (u, v), (-u, v) 순서의 corner 는 normal 쪽에서 보면 glTF 의 앞면인 counter-clockwise 다.
WALL = SimpleNamespace(name='wall', material=0, base_color=(0.8, 0.8, 0.8, 1.0), faces=[
    SimpleNamespace(name='front', center=(0.0, 0.0, 0.0), normal=(0.0, 0.0, 1.0), u=(1.0, 0.0, 0.0), half_u=6.0, half_v=2.0),
])
BOX_MIN = (1.0, -1.5, 0.5)
BOX_MAX = (4.0, 1.5, 3.5)


def box_faces():
    center = [(low + high) / 2 for low, high in zip(BOX_MIN, BOX_MAX)]
    half = [(high - low) / 2 for low, high in zip(BOX_MIN, BOX_MAX)]
    faces = []
    for axis, name in enumerate('xyz'):
        for sign in (1.0, -1.0):
            normal = [0.0, 0.0, 0.0]
            normal[axis] = sign
            u_axis = (axis + 1) % 3
            u = [0.0, 0.0, 0.0]
            u[u_axis] = 1.0
            face_center = list(center)
            face_center[axis] += sign * half[axis]
            faces.append(SimpleNamespace(name=f'{"+" if sign > 0 else "-"}{name}', center=tuple(face_center), normal=tuple(normal),
                                         u=tuple(u), half_u=half[u_axis], half_v=half[(axis + 2) % 3]))
    return faces


BOX = SimpleNamespace(name='box', material=1, base_color=(0.8, 0.1, 0.1, 1.0), faces=box_faces())
SURFACES = [WALL, BOX]


def face_axes(face):
    import numpy as np
    normal, u = np.array(face.normal), np.array(face.u)
    return normal, u, np.cross(normal, u)


def gltf_bounds():
    import numpy as np
    corners = []
    for surface in SURFACES:
        for face in surface.faces:
            normal, u, v = face_axes(face)
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                corners.append(np.array(face.center) + su * face.half_u * u + sv * face.half_v * v)
    return np.min(corners, axis=0), np.max(corners, axis=0)


def write(folder):
    """scene 을 folder 에 쓰고 glTF 의 경로를 돌려준다. material 의 순서는 SURFACES 의 material 과 같다."""
    import numpy as np
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    builder = BufferBuilder()
    meshes, materials, nodes = [], [], []
    for surface in SURFACES:
        positions, normals, tangents, indices = [], [], [], []
        for face in surface.faces:
            normal, u, v = face_axes(face)
            first = len(positions)
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                positions.append(tuple(float(value) for value in np.array(face.center) + su * face.half_u * u + sv * face.half_v * v))
                normals.append(face.normal)
                tangents.append((*face.u, 1.0))
            indices += [first, first + 1, first + 2, first, first + 2, first + 3]
        count = len(positions)
        attributes = {
            'POSITION': builder.add(flatten(positions), GLTF_FLOAT, 'VEC3', count, with_bounds=True),
            'NORMAL': builder.add(flatten(normals), GLTF_FLOAT, 'VEC3', count),
            'TANGENT': builder.add(flatten(tangents), GLTF_FLOAT, 'VEC4', count),
            'TEXCOORD_0': builder.add([0.0] * 2 * count, GLTF_FLOAT, 'VEC2', count),
        }
        materials.append({'name': surface.name, 'pbrMetallicRoughness': {
            'baseColorFactor': list(surface.base_color), 'metallicFactor': 0.0, 'roughnessFactor': 1.0}})
        meshes.append({'name': surface.name, 'primitives': [{
            'attributes': attributes, 'indices': builder.add(indices, GLTF_UNSIGNED_SHORT, 'SCALAR', len(indices)),
            'material': surface.material}]})
        nodes.append({'name': surface.name, 'mesh': len(meshes) - 1})

    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/pickscene.py'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'buffers': [{'uri': 'PickKnownAnswers.bin', 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    (folder / 'PickKnownAnswers.bin').write_bytes(builder.data)
    (folder / GLTF_NAME).write_text(json.dumps(gltf, indent=2), encoding='utf-8')
    return folder / GLTF_NAME


def world_transform(bounds, tolerance=1e-5):
    """state 의 model bounds 로 구한 (배율, 이동). 축마다 배율이 tolerance 보다 다르면 한 배율로 늘린 것이 아니므로 None 이다."""
    import numpy as np
    world_min = np.array([float(bounds['min'][axis]) for axis in 'xyz'])
    world_max = np.array([float(bounds['max'][axis]) for axis in 'xyz'])
    low, high = gltf_bounds()
    scales = (world_max - world_min) / (high - low)
    if scales.max() - scales.min() > tolerance * scales.mean():
        return None
    scale = float(scales.mean())
    return scale, world_min - scale * low


def classify(origin, direction, transform, t_min, t_max):
    """ray 가 처음 맞는 면의 (t, surface, face) 또는 None. 뒷면은 raster 와 pick 처럼 cull 한다."""
    import numpy as np
    scale, offset = transform
    nearest = None
    for surface in SURFACES:
        for face in surface.faces:
            normal, u, v = face_axes(face)
            denominator = direction @ normal
            if denominator >= 0.0:
                continue
            center = scale * np.array(face.center) + offset
            t = ((center - origin) @ normal) / denominator
            if not t_min <= t <= t_max or (nearest is not None and t >= nearest[0]):
                continue
            local = origin + t * direction - center
            if abs(local @ u) <= scale * face.half_u and abs(local @ v) <= scale * face.half_v:
                nearest = (t, surface, face)
    return nearest
