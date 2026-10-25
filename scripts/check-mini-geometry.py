"""glTF 로 만든 .mini 의 geometry 가 glTF 의 triangle 과 node transform 을 그대로 담는지 확인한다.

.mini 는 아래 정의를 따라 읽는다.
  FileHeader, MaterialConstantData, MaterialTextureData, CURRENT_MINI_FILE_VERSION: MiniEngine/Model/Loader/ModelLoader.h
  파일 안의 순서: Renderer::SaveModel (MiniEngine/Model/Loader/ModelConvert.cpp)
  GraphNode, Mesh, Mesh::Draw, PSOFlags: MiniEngine/Model/Model.h
  vertex 와 depth vertex 의 layout: OptimizeMesh (MiniEngine/Model/Loader/MeshConvert.cpp)
  depth vertex 의 stride: MeshSorter::RenderMeshes (MiniEngine/Model/Renderer/Renderer.cpp)

받은 asset 에도 python -I 로 돌릴 수 있게 한다 (scripts/gltffiles.py). 그래서 glTF 폴더 밖이나 scheme 이 있는 uri, buffer
밖을 가리키는 accessor 는 읽기 전에 거부한다. engine 이 무시하는 sparse accessor 도 거부한다. engine 과 같은 틀린 값을 읽어
PASS 할 수 있기 때문이다.

node 의 linear index 는 scene 을 WalkGraph 처럼 depth-first 로 돌며 매긴다. draw 의 vertex 는 다음 draw 의 baseVertex
앞까지 (마지막 draw 는 vbSize / vbStride 앞까지) 다. Mesh 의 draw 는 Mesh::meshCBV 가 가리키는 node 의 glTF mesh 에서
material 이 materialCBV 와, index 수가 primCount 와, POSITION count 가 draw 의 vertex 수와 같으며 아직 짝이 없는 첫
primitive 와 짝짓는다. 판정은 아래와 같고, 하나라도 실패하면 exit 1 이다.

  [0] header.version 이 CURRENT_MINI_FILE_VERSION 과 같다.
  [1] draw 의 triangle 이 짝 primitive 의 triangle 과 multiset 으로 같다. vertex 는 local space position 의 float32 bit 로,
      triangle 은 winding 을 지키는 cyclic rotation 으로 정규화해 비교한다. draw 의 vertex 범위 밖 index 를 가진 triangle 은
      틀린 것으로 센다.
  [2] scene graph 를 ModelInstance::Update 처럼 합성한 matrix 와 glTF node 의 TRS/matrix 로 만든 matrix 로 짝 primitive 의
      POSITION 을 옮긴 위치가 header 의 bounding radius 의 1e-5 배 안에서 같다. app 이 model 에 거는 scale 은 뺀다.
  [3] Mesh 의 draw 가 VB 와 IB 를 0 부터 빈틈 없이 끝까지 채우고, 어느 draw 와도 짝짓지 못한 primitive 가 없다.
  [4] draw 의 index 가 draw 의 vertex 수보다 작다. BLAS 의 VertexCount (vbSize / vbStride - baseVertex, BottomLevelAS.cpp)
      보다 작은지도 따로 센다.
  [5] Mesh 의 VB, depth VB, IB 가 geometry 안에 있고 서로 겹치지 않는다. Mesh 의 offset 은 4 byte 에, draw 의 IB 시작은
      index 크기에 정렬되어 있다. depth VB 의 크기는 vertex 수 x depth stride 다.
  [6] depth VB 의 vertex 마다 position 이 VB 의 position 과 같고, alpha test material 이면 UV 가 VB 의 base color UV 와 같다.
  [7] --same-as 를 주면 두 .mini 가 byte 단위로 같다. header.version 과, CompileMesh 가 채우지 않고 load 할 때 채우는
      Mesh 의 srvTable, samplerTable 은 빼고 비교한다.
  [8] VB 의 UV0, UV1 이 짝 primitive 의 TEXCOORD_0, TEXCOORD_1 과 float32 bit 로 같고, VB 에 UV 가 있는지가 glTF 에
      TEXCOORD 가 있는지와 같다. OptimizeMesh 는 vertex 순서를 바꾸지 않으므로 draw 의 k 번째 vertex 를 짝 primitive 의 k 번째
      vertex 와 비교한다. float 가 아닌 TEXCOORD 는 bit 로 비교할 수 없으므로 틀린 것으로 센다.
  [9] VB 의 tangent 가 짝 primitive 의 TANGENT 를 R10G10B10A2_UNORM 에 [0, 1] 로 옮겨 담은 값이다. xyz 는 10 bit 한 단계
      안에서, w 는 그대로 같다. vertex 는 [8] 처럼 짝짓는다. glTF 에 TANGENT 가 있는데 VB 에 tangent 가 없으면 틀린 것이다.
      TANGENT 가 없는 primitive 의 tangent 는 loader 가 만든 것이라 비교하지 않는다.
  [10] 판정 없이 수치만: normal texture 를 쓰는 primitive 가운데 TANGENT 가 없어 loader 가 tangent 를 만든 것 (MikkTSpace
      가 아니다, OptimizeMesh), 세 vertex 의 TANGENT w 가 같지 않아 spec 이 tangent space 를 undefined 로 두는 triangle
      (Specification.adoc 의 TANGENT 절), w 가 -1 이나 +1 이 아닌 vertex 의 수.
"""

import argparse
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gltffiles import file_in_gltf_folder

REPO_ROOT = Path(__file__).resolve().parent.parent
MODEL_LOADER_HEADER = REPO_ROOT / 'MiniEngine' / 'Model' / 'Loader' / 'ModelLoader.h'

WORLD_TOLERANCE_PER_RADIUS = 1e-5


class Layout:
    """C++ struct 를 field 순서대로 (이름, struct format) 으로 적은 것. '<' 로 읽으므로 padding 은 'x' 로 적는다."""

    def __init__(self, *fields):
        self.fields = fields
        self.struct = struct.Struct('<' + ''.join(fmt for _, fmt in fields))
        self.size = self.struct.size

    def span(self, name):
        formats = [fmt for _, fmt in self.fields]
        index = [field for field, _ in self.fields].index(name)
        return struct.calcsize('<' + ''.join(formats[:index])), struct.calcsize('<' + formats[index])

    def unpack(self, data, offset):
        values = iter(self.struct.unpack_from(data, offset))
        record = SimpleNamespace(offset=offset)
        for name, fmt in self.fields:
            if fmt.endswith('x'):
                continue
            if fmt.endswith('s') or len(fmt) == 1:
                setattr(record, name, next(values))
            else:
                setattr(record, name, [next(values) for _ in range(int(fmt[:-1]))])
        return record


FILE_HEADER = Layout(
    ('id', '4s'), ('version', 'I'), ('numNodes', 'I'), ('numMeshes', 'I'), ('numMaterials', 'I'), ('meshDataSize', 'I'),
    ('numTextures', 'I'), ('stringTableSize', 'I'), ('geometrySize', 'I'), ('keyFrameDataSize', 'I'),
    ('numAnimationCurves', 'I'), ('numAnimations', 'I'), ('numJoints', 'I'),
    ('boundingSphere', '4f'), ('minPos', '3f'), ('maxPos', '3f'))
GRAPH_NODE = Layout(('xform', '16f'), ('rotation', '4f'), ('scale', '3f'), ('bitfield', 'I'))
MESH = Layout(
    ('bounds', '4f'), ('vbOffset', 'I'), ('vbSize', 'I'), ('vbDepthOffset', 'I'), ('vbDepthSize', 'I'),
    ('ibOffset', 'I'), ('ibSize', 'I'), ('vbStride', 'B'), ('ibFormat', 'B'), ('meshCBV', 'H'), ('materialCBV', 'H'),
    ('srvTable', 'H'), ('samplerTable', 'H'), ('psoFlags', 'H'), ('pso', 'H'), ('numJoints', 'H'), ('startJoint', 'H'),
    ('numDraws', 'H'))
DRAW = Layout(('primCount', 'I'), ('startIndex', 'I'), ('baseVertex', 'I'))
MATERIAL_CONSTANT_DATA = Layout(
    ('baseColorFactor', '4f'), ('emissiveFactor', '3f'), ('normalTextureScale', 'f'), ('metallicFactor', 'f'),
    ('roughnessFactor', 'f'), ('flags', 'I'))
MATERIAL_TEXTURE_DATA = Layout(('stringIdx', '5H'), ('', '2x'), ('addressModes', 'I'))

MESH_FIELDS_FILLED_AT_LOAD = ('srvTable', 'samplerTable')

PSO_HAS_TANGENT = 0x004
PSO_HAS_UV0 = 0x008
PSO_HAS_UV1 = 0x010
PSO_ALPHA_TEST = 0x040

DXGI_FORMAT_R32_UINT = 42

POSITION_SIZE = 12
PACKED_ATTRIBUTE_SIZE = 4
UV_SIZE = 8
SKIN_SIZE = 16
# [-1, 1] 을 10 bit 에 담을 때 한 단계의 폭. 담을 때 반올림하는지 버리는지는 DirectXMesh 가 정하므로 한 단계까지 허용한다.
PACKED_XYZ_STEP = 2.0 / 1023.0

GLTF_FLOAT = 5126
GLTF_INDEX_DTYPES = {5121: '<u1', 5123: '<u2', 5125: '<u4'}
GLTF_COMPONENT_SIZES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
GLTF_TYPE_COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT2': 4, 'MAT3': 9, 'MAT4': 16}


def current_mini_file_version():
    text = MODEL_LOADER_HEADER.read_text(encoding='utf-8')
    match = re.search(r'^#define CURRENT_MINI_FILE_VERSION (\d+)', text, re.MULTILINE)
    if match is None:
        raise ValueError(f'CURRENT_MINI_FILE_VERSION is not defined in {MODEL_LOADER_HEADER}')
    return int(match.group(1))


def is_skeleton_root(node):
    return bool(node.bitfield >> 31 & 1)


def has_children(node):
    return bool(node.bitfield >> 29 & 1)


def has_sibling(node):
    return bool(node.bitfield >> 28 & 1)


def matrix_index(node):
    return node.bitfield & 0x0FFFFFFF


def column_major(floats):
    return np.array(floats, dtype=np.float64).reshape(4, 4).T


def regions_of(header, file_size):
    sizes = [
        ('header', FILE_HEADER.size),
        ('geometry', header.geometrySize),
        ('scene graph', header.numNodes * GRAPH_NODE.size),
        ('Mesh records', header.meshDataSize),
        ('material constants', header.numMaterials * MATERIAL_CONSTANT_DATA.size),
        ('material textures', header.numMaterials * MATERIAL_TEXTURE_DATA.size),
        ('texture names', header.stringTableSize),
        ('texture options', header.numTextures),
    ]
    regions = {}
    start = 0
    for name, size in sizes:
        regions[name] = (start, start + size)
        start += size
    regions['animation and joints'] = (start, max(start, file_size))
    return regions


def read_mini(path):
    data = path.read_bytes()
    if len(data) < FILE_HEADER.size:
        raise ValueError(f'{path} is shorter than a .mini header')
    header = FILE_HEADER.unpack(data, 0)
    if header.id != b'MINI':
        raise ValueError(f'{path} is not a .mini file')
    regions = regions_of(header, len(data))
    if regions['texture options'][1] > len(data):
        raise ValueError(f'{path} is shorter than its header says')

    geometry = data[slice(*regions['geometry'])]
    nodes_start = regions['scene graph'][0]
    nodes = [GRAPH_NODE.unpack(data, nodes_start + i * GRAPH_NODE.size) for i in range(header.numNodes)]
    meshes = []
    offset, end = regions['Mesh records']
    while offset + MESH.size <= end:
        mesh = MESH.unpack(data, offset)
        mesh.draws = [DRAW.unpack(data, offset + MESH.size + k * DRAW.size) for k in range(mesh.numDraws)]
        meshes.append(mesh)
        offset += MESH.size + DRAW.size * mesh.numDraws
    if len(meshes) != header.numMeshes or offset != end:
        raise ValueError(f'{path}: the Mesh records do not add up to numMeshes {header.numMeshes} and meshDataSize {header.meshDataSize}')
    return SimpleNamespace(path=path, data=data, header=header, regions=regions, geometry=geometry, nodes=nodes, meshes=meshes)


def element(gltf, key, index):
    # Python list 는 음수 index 로 다른 원소를 돌려주므로 범위를 직접 본다.
    array = gltf.get(key, [])
    if type(index) is not int or not 0 <= index < len(array):
        raise ValueError(f'{key}[{index!r}] does not exist')
    return array[index]


def non_negative_int(value, name):
    if type(value) is not int or value < 0:
        raise ValueError(f'{name} is {value!r}, not a non-negative integer')
    return value


def accessor_layout(gltf, buffers, accessor_index):
    """accessor 가 buffer 에서 차지하는 자리. buffer 나 bufferView 밖에 걸치면 거부한다."""
    name = f'accessor {accessor_index}'
    accessor = element(gltf, 'accessors', accessor_index)
    if 'sparse' in accessor:
        raise ValueError(f'{name} is sparse, which the engine ignores')
    if 'bufferView' not in accessor:
        raise ValueError(f'{name} has no bufferView')
    if accessor.get('componentType') not in GLTF_COMPONENT_SIZES or accessor.get('type') not in GLTF_TYPE_COMPONENTS:
        raise ValueError(f'{name} has an unknown componentType or type')
    element_size = GLTF_COMPONENT_SIZES[accessor['componentType']] * GLTF_TYPE_COMPONENTS[accessor['type']]

    view = element(gltf, 'bufferViews', accessor['bufferView'])
    element(gltf, 'buffers', view.get('buffer'))
    data = buffers[view['buffer']]
    stride = non_negative_int(view.get('byteStride', element_size), f'{name}: byteStride')
    if stride < element_size:
        raise ValueError(f'{name}: byteStride {stride} is smaller than its {element_size} B element')
    view_start = non_negative_int(view.get('byteOffset', 0), f'{name}: bufferView byteOffset')
    view_end = view_start + non_negative_int(view.get('byteLength'), f'{name}: bufferView byteLength')
    start = view_start + non_negative_int(accessor.get('byteOffset', 0), f'{name}: byteOffset')
    count = non_negative_int(accessor.get('count'), f'{name}: count')
    end = start + (count - 1) * stride + element_size if count else start
    if view_end > len(data) or end > view_end:
        raise ValueError(f'{name} reaches past its bufferView or buffer')
    return SimpleNamespace(data=data, start=start, stride=stride, count=count, element_size=element_size)


def accessor_rows(gltf, buffers, accessor_index, dtype):
    layout = accessor_layout(gltf, buffers, accessor_index)
    components = layout.element_size // np.dtype(dtype).itemsize
    if layout.count == 0:
        return np.zeros((0, components), dtype=dtype)
    raw = np.frombuffer(layout.data, dtype=np.uint8, count=(layout.count - 1) * layout.stride + layout.element_size,
                        offset=layout.start)
    rows = np.lib.stride_tricks.as_strided(raw, shape=(layout.count, layout.element_size), strides=(layout.stride, 1))
    return np.ascontiguousarray(rows).view(dtype)


def read_gltf(path):
    raw = path.read_bytes()
    binary_chunk = b''
    if raw[:4] == b'glTF':
        json_length = struct.unpack_from('<I', raw, 12)[0]
        text = raw[20:20 + json_length]
        binary_start = 20 + json_length
        if len(raw) >= binary_start + 8:
            binary_length = struct.unpack_from('<I', raw, binary_start)[0]
            binary_chunk = raw[binary_start + 8:binary_start + 8 + binary_length]
    else:
        text = raw
    gltf = json.loads(text)

    for image in gltf.get('images', []):
        if 'uri' in image:
            file_in_gltf_folder(path, image['uri'])
    buffers = [file_in_gltf_folder(path, buffer['uri']).read_bytes() if 'uri' in buffer else binary_chunk
               for buffer in gltf.get('buffers', [])]
    for accessor_index in range(len(gltf.get('accessors', []))):
        accessor_layout(gltf, buffers, accessor_index)
    return gltf, buffers


def position_count(gltf, primitive):
    return element(gltf, 'accessors', primitive['attributes']['POSITION'])['count']


def index_count(gltf, primitive):
    if 'indices' in primitive:
        return element(gltf, 'accessors', primitive['indices'])['count']
    return position_count(gltf, primitive)


def primitive_geometry(gltf, buffers, primitive):
    """(POSITION 의 float32 bit (vertex, 3), index)"""
    position_index = primitive['attributes']['POSITION']
    position = element(gltf, 'accessors', position_index)
    if position['componentType'] != GLTF_FLOAT or position['type'] != 'VEC3':
        raise ValueError('POSITION is not a float VEC3')
    position_bits = accessor_rows(gltf, buffers, position_index, '<u4')
    if 'indices' not in primitive:
        return position_bits, np.arange(len(position_bits), dtype=np.int64)
    indices = element(gltf, 'accessors', primitive['indices'])
    if indices['componentType'] not in GLTF_INDEX_DTYPES or indices['type'] != 'SCALAR':
        raise ValueError(f'indices accessor {primitive["indices"]} is not an unsigned integer SCALAR')
    dtype = GLTF_INDEX_DTYPES[indices['componentType']]
    return position_bits, accessor_rows(gltf, buffers, primitive['indices'], dtype).ravel().astype(np.int64)


def local_matrix(node):
    if 'matrix' in node:
        return column_major(node['matrix'])
    x, y, z, w = node.get('rotation', [0.0, 0.0, 0.0, 1.0])
    rotation = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])
    matrix = np.identity(4)
    matrix[:3, :3] = rotation * np.array(node.get('scale', [1.0, 1.0, 1.0]))
    matrix[:3, 3] = node.get('translation', [0.0, 0.0, 0.0])
    return matrix


def walk_scene(gltf):
    """scene 의 node 를 WalkGraph 가 매기는 linear index 순서로 돌려준다."""
    if 'scene' not in gltf:
        raise ValueError('the glTF has no default scene, so the loader builds no .mini')
    walked = []
    visited = set()
    pending = [(node_index, np.identity(4)) for node_index in reversed(element(gltf, 'scenes', gltf['scene']).get('nodes', []))]
    while pending:
        node_index, parent_world = pending.pop()
        node = element(gltf, 'nodes', node_index)
        if node_index in visited:
            raise ValueError(f'node {node_index} is reached twice, so the node graph is not a tree')
        visited.add(node_index)
        world = parent_world @ local_matrix(node)
        # glTF::Asset::ProcessNodes 는 camera 가 있는 node 의 mesh 를 읽지 않는다.
        mesh = None if 'camera' in node else node.get('mesh')
        if mesh is not None:
            element(gltf, 'meshes', mesh)
        walked.append(SimpleNamespace(node=node_index, mesh=mesh, world=world))
        pending += [(child, world) for child in reversed(node.get('children', []))]
    return walked


def mini_world_matrices(nodes):
    worlds = [None] * len(nodes)
    stack = []
    parent_world = np.identity(4)
    for node in nodes:
        world = column_major(node.xform)
        if not is_skeleton_root(node):
            world = parent_world @ world
        if matrix_index(node) < len(worlds):
            worlds[matrix_index(node)] = world
        if has_children(node):
            if has_sibling(node):
                stack.append(parent_world)
            parent_world = world
        elif not has_sibling(node):
            if not stack:
                break
            parent_world = stack.pop()
    return worlds


def triangle_keys(corners):
    """corners (triangle, 3, 3) 의 uint32 bit 를, winding 을 지키는 rotation 가운데 사전순으로 가장 작은 것의 byte 로 만든다."""
    rotations = [np.roll(corners, -k, axis=1).reshape(len(corners), 9).astype('>u4').view('S36').ravel() for k in range(3)]
    keys = rotations[0]
    for rotation in rotations[1:]:
        keys = np.where(rotation < keys, rotation, keys)
    return keys


def multiset_difference(keys, other_keys):
    counts, other_counts = Counter(keys.tolist()), Counter(other_keys.tolist())
    return sum((counts - other_counts).values()), sum((other_counts - counts).values())


def depth_stride(mesh):
    return POSITION_SIZE + UV_SIZE * bool(mesh.psoFlags & PSO_ALPHA_TEST) + SKIN_SIZE * (mesh.numJoints > 0)


def uv_offsets(mesh):
    """UV set 마다 VB 의 vertex 안에서 그 UV 의 offset. VB 에 없는 UV 는 None"""
    offset = POSITION_SIZE + PACKED_ATTRIBUTE_SIZE
    if mesh.psoFlags & PSO_HAS_TANGENT:
        offset += PACKED_ATTRIBUTE_SIZE
    uv0 = offset if mesh.psoFlags & PSO_HAS_UV0 else None
    if uv0 is not None:
        offset += UV_SIZE
    return [uv0, offset if mesh.psoFlags & PSO_HAS_UV1 else None]


def base_color_uv_offset(mesh, material):
    """depth VB 의 UV 가 복사해 온 VB 의 UV 의 offset. 그 UV 가 VB 에 없으면 None"""
    texture_info = material.get('pbrMetallicRoughness', {}).get('baseColorTexture', {})
    # glTF::Material::baseColorUV 가 1 bit 라서 loader 는 texCoord 의 마지막 bit 만 본다.
    return uv_offsets(mesh)[texture_info.get('texCoord', 0) & 1]


def rows_in_geometry(geometry, offset, size, stride):
    if stride == 0 or offset + size > len(geometry):
        return None
    count = size // stride
    return np.frombuffer(geometry, dtype=np.uint8, count=count * stride, offset=offset).reshape(count, stride)


def view_of_mesh(mesh, geometry):
    index_size = 4 if mesh.ibFormat == DXGI_FORMAT_R32_UINT else 2
    vertex_count = mesh.vbSize // mesh.vbStride if mesh.vbStride else 0
    vertex_rows = rows_in_geometry(geometry, mesh.vbOffset, mesh.vbSize, mesh.vbStride)
    depth_rows = rows_in_geometry(geometry, mesh.vbDepthOffset, mesh.vbDepthSize, depth_stride(mesh))
    ends = [draw.baseVertex for draw in mesh.draws[1:]] + [vertex_count]
    return SimpleNamespace(
        mesh=mesh, index_size=index_size, vertex_count=vertex_count, vertex_rows=vertex_rows, depth_rows=depth_rows,
        positions=None if vertex_rows is None else np.ascontiguousarray(vertex_rows[:, :POSITION_SIZE]).view('<u4'),
        draw_vertex_counts=[end - draw.baseVertex for draw, end in zip(mesh.draws, ends)])


def draw_indices(geometry, view, draw):
    start = view.mesh.ibOffset + draw.startIndex * view.index_size
    end = start + draw.primCount * view.index_size
    if end > view.mesh.ibOffset + view.mesh.ibSize or end > len(geometry):
        return None
    return np.frombuffer(geometry, dtype=f'<u{view.index_size}', count=draw.primCount, offset=start).astype(np.int64)


def pair_draws_with_primitives(mini, gltf, buffers, walked, views):
    """draw 마다의 짝 primitive 와, 어느 draw 와도 짝짓지 못한 primitive 를 돌려준다."""
    paired = {n: set() for n in range(len(walked))}
    draws = []
    for mesh_index, (mesh, view) in enumerate(zip(mini.meshes, views)):
        node = walked[mesh.meshCBV] if mesh.meshCBV < len(walked) else None
        primitives = gltf['meshes'][node.mesh]['primitives'] if node is not None and node.mesh is not None else []
        for draw_index, draw in enumerate(mesh.draws):
            vertex_count = view.draw_vertex_counts[draw_index]
            primitive_index = next((p for p, primitive in enumerate(primitives)
                                    if p not in paired[mesh.meshCBV]
                                    and primitive.get('material') == mesh.materialCBV
                                    and primitive.get('mode', 4) == 4
                                    and index_count(gltf, primitive) == draw.primCount
                                    and position_count(gltf, primitive) == vertex_count), None)
            case = SimpleNamespace(
                mesh_index=mesh_index, mesh=mesh, view=view, draw_index=draw_index, draw=draw,
                vertex_count=vertex_count, indices=draw_indices(mini.geometry, view, draw),
                primitive_index=primitive_index, primitive=None, position_bits=None, primitive_indices=None)
            if primitive_index is not None:
                paired[mesh.meshCBV].add(primitive_index)
                case.primitive = primitives[primitive_index]
                case.position_bits, case.primitive_indices = primitive_geometry(gltf, buffers, case.primitive)
            draws.append(case)

    unpaired = []
    for n, node in enumerate(walked):
        if node.mesh is not None:
            for p in range(len(gltf['meshes'][node.mesh]['primitives'])):
                if p not in paired[n]:
                    unpaired.append(f'node {n} (glTF node {node.node}) primitive {p}')
    return draws, unpaired


def label_of(mesh_index, mesh):
    return f'mesh {mesh_index} (node {mesh.meshCBV}, material {mesh.materialCBV})'


def triangle_problems(case, totals):
    if case.primitive_index is None:
        return ['no glTF primitive of the node has this material, index count and vertex count']
    if case.indices is None:
        return ['the index range is outside the Mesh IB']
    if case.view.positions is None:
        return ['the Mesh VB is outside the geometry']
    if len(case.indices) % 3:
        return [f'primCount {case.draw.primCount} is not a multiple of 3']
    if len(case.primitive_indices) and case.primitive_indices.max() >= len(case.position_bits):
        return ['the glTF primitive has an index beyond its POSITION count']

    triangles = case.indices.reshape(-1, 3)
    bound = min(case.vertex_count, case.view.vertex_count - case.draw.baseVertex)
    in_range = np.all(triangles < bound, axis=1)
    out_of_range = int(np.count_nonzero(~in_range))
    mini_keys = triangle_keys(case.view.positions[case.draw.baseVertex + triangles[in_range]])
    gltf_keys = triangle_keys(case.position_bits[case.primitive_indices.reshape(-1, 3)])
    only_mini, only_gltf = multiset_difference(mini_keys, gltf_keys)

    totals.update(triangles=len(triangles), only_mini=only_mini, only_gltf=only_gltf, out_of_range=out_of_range)
    if only_mini or only_gltf or out_of_range:
        return [f'{only_mini} triangles only in .mini, {only_gltf} only in glTF, {out_of_range} with an index outside the draw']
    return []


def world_problems(case, mini_worlds, walked, tolerance, largest):
    if case.position_bits is None:
        return ['no glTF primitive to compare with']
    node = case.mesh.meshCBV
    if node >= len(mini_worlds) or mini_worlds[node] is None:
        return [f'node {node} has no matrix in the scene graph']
    positions = case.position_bits.view('<f4').astype(np.float64)
    points = np.hstack([positions, np.ones((len(positions), 1))])
    difference = float(np.linalg.norm(points @ (mini_worlds[node] - walked[node].world).T, axis=1).max())
    largest[0] = max(largest[0], difference)
    return [f'world position differs by {difference:g}, more than {tolerance:g}'] if difference > tolerance else []


def layout_problems(view, cases):
    mesh = view.mesh
    problems = []
    if mesh.vbStride == 0 or mesh.vbSize % mesh.vbStride:
        problems.append((None, f'vbSize {mesh.vbSize} is not a multiple of vbStride {mesh.vbStride}'))
    if mesh.ibSize % view.index_size:
        problems.append((None, f'ibSize {mesh.ibSize} is not a multiple of the {view.index_size} B index'))

    next_vertex = next_index = 0
    for case in cases:
        k, draw = case.draw_index, case.draw
        if draw.baseVertex != next_vertex:
            problems.append((k, f'baseVertex {draw.baseVertex}, not {next_vertex} right after the previous draw'))
        if draw.startIndex != next_index:
            problems.append((k, f'startIndex {draw.startIndex}, not {next_index} right after the previous draw'))
        if case.vertex_count <= 0:
            problems.append((k, f'{case.vertex_count} vertices before the next draw or the VB end'))
        next_vertex = draw.baseVertex + max(case.vertex_count, 0)
        next_index = draw.startIndex + draw.primCount
    if next_index != mesh.ibSize // view.index_size:
        problems.append((None, f'the draws end at index {next_index}, not at the IB end {mesh.ibSize // view.index_size}'))
    return problems


def index_problems(case):
    if case.indices is None:
        return ['the index range is outside the Mesh IB'], False
    largest = int(case.indices.max()) if len(case.indices) else -1
    blas_vertex_count = case.view.vertex_count - case.draw.baseVertex
    problems = []
    if largest >= case.vertex_count:
        problems.append(f'largest index {largest} is not below the draw vertex count {case.vertex_count}')
    if largest >= blas_vertex_count:
        problems.append(f'largest index {largest} is not below the BLAS VertexCount {blas_vertex_count}')
    return problems, largest >= blas_vertex_count


def range_problems(views, geometry_size):
    """Mesh 마다의 문제. 겹치면 뒤에 놓인 구간의 Mesh 에 적는다."""
    problems = [[] for _ in views]
    ranges = []
    for i, view in enumerate(views):
        mesh = view.mesh
        for name, offset, size in (('VB', mesh.vbOffset, mesh.vbSize), ('depth VB', mesh.vbDepthOffset, mesh.vbDepthSize),
                                   ('IB', mesh.ibOffset, mesh.ibSize)):
            if offset + size > geometry_size:
                problems[i].append((None, f'{name} [{offset}, {offset + size}) is outside the {geometry_size} B geometry'))
            if offset % 4:
                problems[i].append((None, f'{name} offset {offset} is not 4 byte aligned'))
            if size:
                ranges.append((offset, offset + size, i, name))
        expected_depth_size = view.vertex_count * depth_stride(mesh)
        if mesh.vbDepthSize != expected_depth_size:
            problems[i].append((None, f'vbDepthSize {mesh.vbDepthSize}, not {view.vertex_count} vertices x {depth_stride(mesh)} B'))
        for k, draw in enumerate(mesh.draws):
            start = mesh.ibOffset + draw.startIndex * view.index_size
            if start % view.index_size:
                problems[i].append((k, f'IB starts at {start}, not aligned to the {view.index_size} B index'))

    ranges.sort()
    covered_end, covered_by = 0, None
    for start, end, i, name in ranges:
        if start < covered_end:
            problems[i].append((None, f'{name} [{start}, {end}) overlaps {covered_by}'))
        if end > covered_end:
            covered_end, covered_by = end, f'the {name} of mesh {i}'
    return problems


def depth_problems(case, material):
    view = case.view
    rows = slice(case.draw.baseVertex, case.draw.baseVertex + max(case.vertex_count, 0))
    if view.vertex_rows is None or view.depth_rows is None:
        return ['the VB or depth VB is outside the geometry']
    if rows.stop > len(view.vertex_rows) or rows.stop > len(view.depth_rows):
        return [f'vertices [{rows.start}, {rows.stop}) are beyond the VB or the depth VB']
    vertices, depth_vertices = view.vertex_rows[rows], view.depth_rows[rows]

    problems = []
    position_mismatches = int(np.count_nonzero(np.any(depth_vertices[:, :POSITION_SIZE] != vertices[:, :POSITION_SIZE], axis=1)))
    if position_mismatches:
        problems.append(f'{position_mismatches} of {len(vertices)} depth positions differ from the VB')
    if case.mesh.psoFlags & PSO_ALPHA_TEST:
        uv_offset = base_color_uv_offset(case.mesh, material)
        uv, depth_uv = uv_bytes(vertices, uv_offset), uv_bytes(depth_vertices, POSITION_SIZE)
        if uv_offset is None:
            problems.append('alpha test material, but the VB has no base color UV')
        elif uv is None or depth_uv is None:
            problems.append(f'the base color UV reaches past the vbStride {case.mesh.vbStride} or the depth stride {depth_stride(case.mesh)}')
        else:
            uv_mismatches = int(np.count_nonzero(np.any(depth_uv != uv, axis=1)))
            if uv_mismatches:
                problems.append(f'{uv_mismatches} of {len(vertices)} depth UVs differ from the VB')
    return problems


def uv_bytes(vertices, offset):
    """vertex 마다 offset 에서 시작하는 UV 의 byte. UV 가 없거나 vertex 끝을 넘으면 None"""
    if offset is None or offset + UV_SIZE > vertices.shape[1]:
        return None
    return vertices[:, offset:offset + UV_SIZE]


def uv_problems(case, gltf, buffers, totals):
    if case.primitive is None:
        return ['no glTF primitive to compare with']
    view = case.view
    rows = slice(case.draw.baseVertex, case.draw.baseVertex + max(case.vertex_count, 0))
    if view.vertex_rows is None or rows.stop > len(view.vertex_rows):
        return [f'vertices [{rows.start}, {rows.stop}) are beyond the VB']
    vertices = view.vertex_rows[rows]

    problems = []
    for uv_set, offset in enumerate(uv_offsets(case.mesh)):
        semantic = f'TEXCOORD_{uv_set}'
        accessor_index = case.primitive['attributes'].get(semantic)
        if accessor_index is None or offset is None:
            if accessor_index is not None:
                problems.append(f'the glTF primitive has {semantic}, but the VB has no UV{uv_set}')
            elif offset is not None:
                problems.append(f'the VB has UV{uv_set}, but the glTF primitive has no {semantic}')
            continue
        accessor = element(gltf, 'accessors', accessor_index)
        if accessor['componentType'] != GLTF_FLOAT or accessor['type'] != 'VEC2':
            problems.append(f'{semantic} is not a float VEC2, so its bits cannot be compared')
            continue
        expected = accessor_rows(gltf, buffers, accessor_index, '<u4')
        if len(expected) != len(vertices):
            problems.append(f'{semantic} has {len(expected)} values for {len(vertices)} vertices')
            continue
        uv = uv_bytes(vertices, offset)
        if uv is None:
            problems.append(f'UV{uv_set} at offset {offset} reaches past the vbStride {case.mesh.vbStride}')
            continue
        actual = np.ascontiguousarray(uv).view('<u4')
        mismatches = int(np.count_nonzero(np.any(actual != expected, axis=1)))
        totals.update(uvs=len(expected), mismatches=mismatches)
        if mismatches:
            problems.append(f'{mismatches} of {len(expected)} UV{uv_set} differ from {semantic}')
    return problems


def tangent_offset(mesh):
    """VB 의 vertex 안에서 tangent 의 offset. VB 에 tangent 가 없으면 None"""
    return POSITION_SIZE + PACKED_ATTRIBUTE_SIZE if mesh.psoFlags & PSO_HAS_TANGENT else None


def unpack_signed_r10g10b10a2(packed):
    """HitSurface.hlsli 의 UnpackSignedR10G10B10A2 처럼 [0, 1] 로 담은 [-1, 1] 값을 되돌린다."""
    stored = np.stack([packed & 0x3FF, packed >> 10 & 0x3FF, packed >> 20 & 0x3FF, packed >> 30], axis=1)
    return stored / np.array([1023.0, 1023.0, 1023.0, 3.0]) * 2.0 - 1.0


def gltf_tangents(gltf, buffers, primitive):
    """짝 primitive 의 TANGENT (vertex, 4) 와, 읽지 못했으면 그 이유. TANGENT 가 없으면 둘 다 None"""
    accessor_index = primitive['attributes'].get('TANGENT')
    if accessor_index is None:
        return None, None
    accessor = element(gltf, 'accessors', accessor_index)
    if accessor['componentType'] != GLTF_FLOAT or accessor['type'] != 'VEC4':
        return None, f'TANGENT accessor {accessor_index} is not a float VEC4'
    return accessor_rows(gltf, buffers, accessor_index, '<f4').astype(np.float64), None


def tangent_problems(case, gltf, buffers, totals):
    if case.primitive is None:
        return ['no glTF primitive to compare with']
    expected, unreadable = gltf_tangents(gltf, buffers, case.primitive)
    if unreadable:
        return [unreadable]
    if expected is None:
        totals.update(without_tangent=1)
        return []
    offset = tangent_offset(case.mesh)
    if offset is None:
        return ['the glTF primitive has TANGENT, but the VB has no tangent']
    view = case.view
    rows = slice(case.draw.baseVertex, case.draw.baseVertex + max(case.vertex_count, 0))
    if view.vertex_rows is None or rows.stop > len(view.vertex_rows):
        return [f'vertices [{rows.start}, {rows.stop}) are beyond the VB']
    vertices = view.vertex_rows[rows]
    if len(expected) != len(vertices):
        return [f'TANGENT has {len(expected)} values for {len(vertices)} vertices']

    actual = unpack_signed_r10g10b10a2(np.ascontiguousarray(vertices[:, offset:offset + PACKED_ATTRIBUTE_SIZE]).view('<u4').ravel())
    differs = np.any(np.abs(actual[:, :3] - expected[:, :3]) > PACKED_XYZ_STEP, axis=1) | (actual[:, 3] != expected[:, 3])
    mismatches = int(np.count_nonzero(differs))
    totals.update(tangents=len(expected), mismatches=mismatches)
    return [f'{mismatches} of {len(expected)} tangents differ from TANGENT'] if mismatches else []


def count_normal_mapped_tangents(case, gltf, buffers, material, counts):
    if case.primitive is None or 'normalTexture' not in material:
        return
    counts.update(primitives=1)
    tangents, unreadable = gltf_tangents(gltf, buffers, case.primitive)
    if tangents is None and not unreadable:
        counts.update(made_by_loader=1)
    # 읽지 못한 TANGENT 와 POSITION 밖의 index 는 [9] 와 [1] 이 틀린 것으로 센다.
    if tangents is None or (len(case.primitive_indices) and case.primitive_indices.max() >= len(tangents)):
        return
    w = tangents[:, 3]
    triangle_w = w[case.primitive_indices.reshape(-1, 3)]
    counts.update(triangles=len(triangle_w), unequal_w_triangles=int(np.count_nonzero(np.ptp(triangle_w, axis=1))),
                  vertices=len(w), non_unit_w_vertices=int(np.count_nonzero(np.abs(w) != 1.0)))


def first_difference(mini, other):
    ignored = np.zeros(len(mini.data), dtype=bool)
    version_offset, version_size = FILE_HEADER.span('version')
    ignored[version_offset:version_offset + version_size] = True
    for mesh in mini.meshes:
        for name in MESH_FIELDS_FILLED_AT_LOAD:
            offset, size = MESH.span(name)
            ignored[mesh.offset + offset:mesh.offset + offset + size] = True

    data, other_data = np.frombuffer(mini.data, dtype=np.uint8), np.frombuffer(other.data, dtype=np.uint8)
    common = min(len(data), len(other_data))
    differing = np.flatnonzero((data[:common] != other_data[:common]) & ~ignored[:common])
    if len(differing):
        return int(differing[0])
    return common if len(data) != len(other_data) else None


def region_at(regions, offset):
    return next((name for name, (start, end) in regions.items() if start <= offset < end), 'past the end')


class Tally:
    def __init__(self, max_report):
        self.max_report = max_report
        self.results = {}

    def judge(self, check, subject, problems):
        self.results.setdefault(check, []).append((subject, problems))

    def summary(self, check, unit):
        results = self.results.get(check, [])
        return f'{sum(1 for _, problems in results if not problems)}/{len(results)} {unit} PASS'

    def failures(self, check):
        return [f'{subject}{"" if draw is None else f" draw {draw}"}: {text}'
                for subject, problems in self.results.get(check, []) for draw, text in problems]

    def has_failures(self):
        return any(problems for results in self.results.values() for _, problems in results)

    def report(self, check):
        failures = self.failures(check)
        for failure in failures[:self.max_report]:
            print(f'    {failure}')
        if len(failures) > self.max_report:
            print(f'    ... {len(failures) - self.max_report} more')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('gltf', help='the .gltf or .glb the .mini was built from')
    parser.add_argument('--mini', help='default: the glTF path with the .mini extension, as Renderer::LoadModel names it')
    parser.add_argument('--same-as', metavar='OLD.mini', help='a .mini that must hold the same bytes. Without it [7] is skipped')
    parser.add_argument('--max-report', type=int, default=10, metavar='N', help='failures listed per check (default: %(default)s)')
    args = parser.parse_args()

    gltf_path = Path(args.gltf).resolve()
    mini_path = Path(args.mini).resolve() if args.mini else gltf_path.with_suffix('.mini')
    try:
        expected_version = current_mini_file_version()
        gltf, buffers = read_gltf(gltf_path)
        walked = walk_scene(gltf)
        mini = read_mini(mini_path)
        old_mini = read_mini(Path(args.same_as).resolve()) if args.same_as else None
        views = [view_of_mesh(mesh, mini.geometry) for mesh in mini.meshes]
        cases, unpaired = pair_draws_with_primitives(mini, gltf, buffers, walked, views)
    except (OSError, ValueError, KeyError, IndexError, TypeError, struct.error) as error:
        print(f'error: {type(error).__name__}: {error}', file=sys.stderr)
        return 1

    header = mini.header
    radius = header.boundingSphere[3]
    tolerance = WORLD_TOLERANCE_PER_RADIUS * radius
    mini_worlds = mini_world_matrices(mini.nodes)
    materials = gltf.get('materials', [])

    tally = Tally(args.max_report)
    tally.judge(0, mini_path.name, [] if header.version == expected_version else
                [(None, f'version {header.version}, not CURRENT_MINI_FILE_VERSION {expected_version}')])
    triangle_totals = Counter()
    uv_totals = Counter()
    tangent_totals = Counter()
    normal_mapped = Counter()
    largest_world_difference = [0.0]
    beyond_blas_vertex_count = 0
    for case in cases:
        subject = label_of(case.mesh_index, case.mesh)
        k = case.draw_index
        tally.judge(1, subject, [(k, text) for text in triangle_problems(case, triangle_totals)])
        tally.judge(2, subject, [(k, text) for text in world_problems(case, mini_worlds, walked, tolerance, largest_world_difference)])
        problems, beyond_blas = index_problems(case)
        tally.judge(4, subject, [(k, text) for text in problems])
        beyond_blas_vertex_count += beyond_blas
        material = materials[case.mesh.materialCBV] if case.mesh.materialCBV < len(materials) else {}
        tally.judge(6, subject, [(k, text) for text in depth_problems(case, material)])
        tally.judge(8, subject, [(k, text) for text in uv_problems(case, gltf, buffers, uv_totals)])
        tally.judge(9, subject, [(k, text) for text in tangent_problems(case, gltf, buffers, tangent_totals)])
        count_normal_mapped_tangents(case, gltf, buffers, material, normal_mapped)
    for mesh_index, (view, problems) in enumerate(zip(views, range_problems(views, header.geometrySize))):
        subject = label_of(mesh_index, view.mesh)
        tally.judge(3, subject, layout_problems(view, [case for case in cases if case.mesh_index == mesh_index]))
        tally.judge(5, subject, problems)
    tally.judge('3 unpaired', 'glTF', [(None, f'{primitive} is drawn by no Mesh draw') for primitive in unpaired])
    if old_mini is not None:
        difference = first_difference(mini, old_mini)
        tally.judge(7, f'{mini_path.name} vs {old_mini.path}',
                    [] if difference is None else [(None, f'first difference at offset {difference} ({region_at(mini.regions, difference)})')])

    print(f'version {header.version} (CURRENT_MINI_FILE_VERSION {expected_version}), {header.numMeshes} meshes, {len(cases)} draws, '
          f'at most {max((mesh.numDraws for mesh in mini.meshes), default=0)} draws per Mesh, geometry {header.geometrySize} B')
    print(f'glTF: {gltf_path}')
    print(f'.mini: {mini_path}')
    print(f'[0] header.version == CURRENT_MINI_FILE_VERSION: {tally.summary(0, "files")}')
    tally.report(0)
    print(f'[1] triangles == glTF primitive (float32 bit, winding kept): {tally.summary(1, "draws")}, '
          f'{triangle_totals["only_mini"]} of {triangle_totals["triangles"]} triangles only in .mini, '
          f'{triangle_totals["only_gltf"]} only in glTF, {triangle_totals["out_of_range"]} with an index outside the draw')
    tally.report(1)
    print(f'[2] world position == glTF node transform within {tolerance:g} (1e-5 x radius {radius:g}): '
          f'{tally.summary(2, "draws")}, largest difference {largest_world_difference[0]:g}')
    tally.report(2)
    print(f'[3] draws fill the Mesh VB and IB from 0 without gaps: {tally.summary(3, "meshes")}, '
          f'{len(unpaired)} glTF primitives drawn by no Mesh draw')
    tally.report(3)
    tally.report('3 unpaired')
    print(f'[4] index < draw vertex count: {tally.summary(4, "draws")}, '
          f'{beyond_blas_vertex_count} draws with an index >= the BLAS VertexCount')
    tally.report(4)
    print(f'[5] Mesh ranges inside the geometry, disjoint, aligned, depth VB size == vertex count x depth stride: {tally.summary(5, "meshes")}')
    tally.report(5)
    print(f'[6] depth VB position (and alpha test UV) == VB: {tally.summary(6, "draws")}')
    tally.report(6)
    if old_mini is None:
        print('[7] same bytes as --same-as: skipped, --same-as not given')
    else:
        print(f'[7] same bytes as {old_mini.path} except header.version, srvTable, samplerTable: {tally.summary(7, "files")}')
        tally.report(7)
    print(f'[8] VB UV == glTF TEXCOORD (float32 bit, same vertex order): {tally.summary(8, "draws")}, '
          f'{uv_totals["mismatches"]} of {uv_totals["uvs"]} UVs differ')
    tally.report(8)
    print(f'[9] VB tangent == glTF TANGENT (xyz within one 10 bit step, w exact): {tally.summary(9, "draws")}, '
          f'{tangent_totals["mismatches"]} of {tangent_totals["tangents"]} tangents differ, '
          f'{tangent_totals["without_tangent"]} draws without TANGENT')
    tally.report(9)
    print(f'[10] normal texture primitives: {normal_mapped["primitives"]}, '
          f'{normal_mapped["made_by_loader"]} without TANGENT so the loader makes the tangent, '
          f'{normal_mapped["unequal_w_triangles"]} of {normal_mapped["triangles"]} triangles with unequal TANGENT w, '
          f'{normal_mapped["non_unit_w_vertices"]} of {normal_mapped["vertices"]} vertices with w not -1 or +1')

    failed = tally.has_failures()
    print('FAIL' if failed else 'PASS')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
