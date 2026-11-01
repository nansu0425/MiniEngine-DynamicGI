"""pick 이 돌려준 것을 해석적으로 구한 정답과 비교하는 확인이 함께 쓰는 계산.

camera ray 는 app 의 CameraRays 처럼 구하고, 정답은 그 ray 와 world 의 평행사변형 면들의 교차로 구한다.
"""

import math
from types import SimpleNamespace

# pixel 의 가운데와 네 변의 가운데. 이 sample 들이 모두 같은 것을 보는 pixel 만 판정한다. 그 밖의 pixel 은 정답이 sample
# 위치의 오차에 달려 있다.
PIXEL_EDGE_OFFSETS = [(0.0, 0.0), (-0.5, 0.0), (0.5, 0.0), (0.0, -0.5), (0.0, 0.5)]
# pixel 의 네 모서리. 볼록한 면이 이 넷을 모두 덮으면 pixel 안의 어느 sample 도 그 면을 본다.
PIXEL_CORNER_OFFSETS = [(-0.5, -0.5), (0.5, -0.5), (-0.5, 0.5), (0.5, 0.5)]


def vector_of(fields):
    import numpy as np
    return np.array([float(fields[axis]) for axis in 'xyz'])


def camera_ray(camera, scene, sample):
    """CameraRays.cpp 와 CameraRays.hlsli 처럼, scene 의 pixel 격자에서 sample (client pixel) 을 지나는 ray 의 origin 과 방향.
    camera 는 roll 이 없다고 본다."""
    import numpy as np
    forward = vector_of(camera['forward'])
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, [0.0, 1.0, 0.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    half_height = np.tan(float(camera['vertical_fov']) / 2)
    half_width = half_height * scene['width'] / scene['height']
    # pixel 격자는 frustum 을 반 pixel 왼쪽 위로 옮긴 것이다.
    x, y = sample[0] - scene['left'] - 0.5, sample[1] - scene['top'] - 0.5
    direction = forward + right * half_width * (2 * x / scene['width'] - 1) + up * half_height * (1 - 2 * y / scene['height'])
    return vector_of(camera), direction / np.linalg.norm(direction)


def grid_cells(grid):
    """pick grid 의 응답을 칸마다 (x, y, instance, material, distance, position 의 text) 로 푼다. miss 인 칸의 값은 None 이다."""
    keys = ['instance', 'material', 'distance', 'position']
    cells = []
    for row in range(grid['rows']):
        fields = grid['row'][str(row)]
        tokens = {key: fields[key].split() for key in keys}
        for column, x in enumerate(int(x) for x in grid['x'].split()):
            cells.append((x, fields['y'], {key: None if tokens[key][column] == '-' else tokens[key][column] for key in keys}))
    return cells


def parallelogram(center, half_u, half_v, normal, double_sided, **names):
    """world 의 평행사변형 면. 면은 center + s half_u + t half_v (|s|, |t| <= 1) 이고, normal 쪽이 앞면이다.
    names 는 면을 알아보는 값으로 그대로 남긴다."""
    import numpy as np
    return SimpleNamespace(center=np.asarray(center, dtype=float), half_u=np.asarray(half_u, dtype=float),
                           half_v=np.asarray(half_v, dtype=float), normal=np.asarray(normal, dtype=float), double_sided=double_sided, **names)


def nearest_face(origin, direction, faces, t_min, t_max):
    """ray 가 [t_min, t_max] 에서 처음 맞는 (t, face) 또는 None. pick 처럼 single-sided 면의 뒷면은 cull 한다."""
    import numpy as np
    nearest = None
    for face in faces:
        denominator = direction @ face.normal
        if denominator == 0.0 or (denominator > 0.0 and not face.double_sided):
            continue
        t = ((face.center - origin) @ face.normal) / denominator
        if not t_min <= t <= t_max or (nearest is not None and t >= nearest[0]):
            continue
        local = origin + t * direction - face.center
        # half_u 와 half_v 가 직교하지 않아도 되도록 dual basis 로 면 위의 좌표를 구한다.
        dual_u, dual_v = np.cross(face.half_v, face.normal), np.cross(face.normal, face.half_u)
        s, r = (local @ dual_u) / (face.half_u @ dual_u), (local @ dual_v) / (face.half_v @ dual_v)
        if abs(s) <= 1.0 and abs(r) <= 1.0:
            nearest = (t, face)
    return nearest


def seen_faces(camera, scene, faces, offset=(0.0, 0.0)):
    """scene 의 모든 pixel 에서, pixel 의 가운데를 offset (pixel) 만큼 옮긴 sample 이 near 와 far 사이에서 처음 맞는 면의 faces 안
    index. miss 는 -1 이다. pixel 마다 camera_ray 와 nearest_face 를 부르는 것과 같은 계산을 배열로 한 번에 한다."""
    import numpy as np
    forward = vector_of(camera['forward'])
    unit_forward = forward / np.linalg.norm(forward)
    right = np.cross(unit_forward, [0.0, 1.0, 0.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, unit_forward)
    half_height = np.tan(float(camera['vertical_fov']) / 2)
    half_width = half_height * scene['width'] / scene['height']
    y, x = np.mgrid[0:scene['height'], 0:scene['width']].astype(np.float64)
    x, y = x + offset[0], y + offset[1]
    direction = (unit_forward + right * half_width * (2 * x / scene['width'] - 1)[..., None]
                 + up * half_height * (1 - 2 * y / scene['height'])[..., None])
    direction /= np.linalg.norm(direction, axis=-1, keepdims=True)
    origin = vector_of(camera)
    cosine = direction @ forward
    t_min, t_max = float(camera['near']) / cosine, float(camera['far']) / cosine
    nearest_t, nearest = np.full(cosine.shape, np.inf), np.full(cosine.shape, -1)
    with np.errstate(divide='ignore', invalid='ignore'):
        for index, face in enumerate(faces):
            denominator = direction @ face.normal
            facing = (denominator != 0.0) & ((denominator < 0.0) | face.double_sided)
            t = ((face.center - origin) @ face.normal) / denominator
            local = origin + t[..., None] * direction - face.center
            dual_u, dual_v = np.cross(face.half_v, face.normal), np.cross(face.normal, face.half_u)
            s, r = (local @ dual_u) / (face.half_u @ dual_u), (local @ dual_v) / (face.half_v @ dual_v)
            hit = facing & (t >= t_min) & (t <= t_max) & (t < nearest_t) & (np.abs(s) <= 1.0) & (np.abs(r) <= 1.0)
            nearest_t, nearest = np.where(hit, t, nearest_t), np.where(hit, index, nearest)
    return nearest


def is_unambiguous(seen_at, pixel, offsets=PIXEL_EDGE_OFFSETS):
    """seen_at(sample) 이 pixel 의 가운데에서 offsets 만큼 옮긴 sample 마다 같은 값을 돌려주는가. seen_at 의 값은 hash 할 수 있어야 한다."""
    import numpy as np
    center = np.array(pixel, dtype=float) + 0.5
    return len({seen_at(center + np.array(offset)) for offset in offsets}) == 1


def pick_matches(values, origin, direction, found):
    """pick grid 의 칸 값 (grid_cells) 이 ray (origin, direction) 의 정답 found (nearest_face 의 결과) 와 같은가. miss 인 칸은 found 가
    None 일 때만 맞다. 맞은 칸은 material 이 같고, 거리와 위치가 거리의 상대 1e-5 안이어야 한다."""
    import numpy as np
    if found is None:
        return values['material'] is None
    t, face = found
    if values['position'] is None:
        return False
    position = np.array([float(value) for value in values['position'].split(',')])
    return values['material'] == str(face.material) and abs(float(values['distance']) - t) <= 1e-5 * t and \
        np.linalg.norm(position - (origin + t * direction)) <= 1e-5 * t


class CameraAnswers:
    """world 의 면 faces (parallelogram 의 목록, 면마다 material 이 있다) 를 state 의 camera 로 볼 때 sample 이 맞는 면"""

    def __init__(self, faces, state):
        self.faces = faces
        self.camera, self.scene = state['camera'], state['ui']['scene']
        self.forward = vector_of(self.camera['forward'])

    def expect(self, sample):
        """(sample 을 지나는 ray 의 origin, 방향, 그 ray 가 near 와 far 사이에서 처음 맞는 (t, face) 또는 None)"""
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
        import numpy as np
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
