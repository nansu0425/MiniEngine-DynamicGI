"""MiniEngine 이 쓰는 image 파일을 numpy 배열로 읽는다.

여러 script 가 같은 파일을 다르게 읽지 않도록 reader 를 여기에 모은다. viewer.py 는 numpy 가 필요 없는 명령에서도
이 module 을 import 하므로 numpy 는 쓰는 함수 안에서 import 한다.
"""

from pathlib import Path

DXGI_FORMAT_R32G32B32A32_FLOAT = 2
DXGI_FORMAT_R8G8B8A8_UNORM = 28
DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29

DDSD_MIPMAPCOUNT = 0x20000
DDPF_ALPHAPIXELS = 0x1
DDPF_FOURCC = 0x4
DDPF_RGB = 0x40
D3DFMT_A32B32G32R32F = 116
RGBA8_MASKS = (0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)


class ImageFileError(Exception):
    pass


def read_dds_header(path):
    """(file bytes, width, height, mip 수, DXGI format, pixel 이 시작하는 offset) 을 돌려준다. 모르는 format 은 None 이다."""
    data = Path(path).read_bytes()
    if data[:4] != b'DDS ':
        raise ImageFileError(f'{path} is not a DDS file')

    def u32(offset):
        return int.from_bytes(data[offset:offset + 4], 'little')

    height, width = u32(12), u32(16)
    mip_count = max(1, u32(28)) if u32(8) & DDSD_MIPMAPCOUNT else 1
    pixel_format_flags, four_cc = u32(80), data[84:88]
    if four_cc == b'DX10':
        return data, width, height, mip_count, u32(128), 148

    # DirectXTex 의 SaveToDDSFile 은 legacy header 로 나타낼 수 있는 DXGI format 을 legacy header 로 쓴다.
    if pixel_format_flags & DDPF_FOURCC and u32(84) == D3DFMT_A32B32G32R32F:
        dxgi_format = DXGI_FORMAT_R32G32B32A32_FLOAT
    elif (pixel_format_flags == DDPF_RGB | DDPF_ALPHAPIXELS and u32(88) == 32
          and (u32(92), u32(96), u32(100), u32(104)) == RGBA8_MASKS):
        dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM
    else:
        dxgi_format = None
    return data, width, height, mip_count, dxgi_format, 128


def read_dds_rgba32f(path):
    """mip 0 을 (height, width, 4) float32 배열로 돌려준다."""
    import numpy as np
    data, width, height, _, dxgi_format, offset = read_dds_header(path)
    if dxgi_format != DXGI_FORMAT_R32G32B32A32_FLOAT:
        raise ImageFileError(f'{path} is not RGBA32F')
    return np.frombuffer(data, dtype='<f4', count=width * height * 4, offset=offset).reshape(height, width, 4)


def read_dds_rgba8(path):
    """(DXGI format, mip 0 부터 모든 mip 의 (height, width, 4) uint8 배열 list) 를 돌려준다."""
    import numpy as np
    data, width, height, mip_count, dxgi_format, offset = read_dds_header(path)
    if dxgi_format not in (DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB):
        raise ImageFileError(f'{path} is DXGI format {dxgi_format}, not R8G8B8A8_UNORM or R8G8B8A8_UNORM_SRGB')
    shapes = [(max(1, height >> level), max(1, width >> level)) for level in range(mip_count)]
    expected_size = offset + sum(4 * mip_height * mip_width for mip_height, mip_width in shapes)
    if expected_size != len(data):
        raise ImageFileError(f'{path} is {len(data)} bytes but {width}x{height} with {mip_count} mips needs {expected_size}')
    mips = []
    for mip_height, mip_width in shapes:
        mips.append(np.frombuffer(data, dtype=np.uint8, count=4 * mip_height * mip_width, offset=offset)
                    .reshape(mip_height, mip_width, 4))
        offset += 4 * mip_height * mip_width
    return dxgi_format, mips
