"""test scene 을 만드는 script 가 glTF 의 buffer 와 accessor 를 쓰는 방법을 여기에 모은다."""

import struct

GLTF_FLOAT = 5126
GLTF_UNSIGNED_SHORT = 5123


class BufferBuilder:
    def __init__(self):
        self.data = bytearray()
        self.views = []
        self.accessors = []

    def add(self, values, component_type, type_name, count, with_bounds=False):
        import numpy as np

        if len(self.data) % 4:
            self.data += b'\0' * (4 - len(self.data) % 4)
        fmt = '<' + ('f' if component_type == GLTF_FLOAT else 'H') * len(values)
        packed = struct.pack(fmt, *values)
        self.views.append({'buffer': 0, 'byteOffset': len(self.data), 'byteLength': len(packed)})
        self.data += packed
        accessor = {'bufferView': len(self.views) - 1, 'componentType': component_type, 'count': count, 'type': type_name}
        if with_bounds:
            rows = np.array(values).reshape(count, -1)
            accessor['min'] = rows.min(axis=0).tolist()
            accessor['max'] = rows.max(axis=0).tolist()
        self.accessors.append(accessor)
        return len(self.accessors) - 1


def flatten(rows):
    return [value for row in rows for value in row]
