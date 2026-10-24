"""glTF texture 의 DDS 가 그 texture 를 쓰는 material slot 의 transfer function 대로 만들어졌는지 확인한다.

loader 가 변환 flag 를 material slot 에서 정하므로 slot 도 glTF 를 parse 해 정한다. base color 와 emissive 는 sRGB,
metallicRoughness, normal, occlusion 은 linear 다. 판정은 아래와 같고, 하나라도 실패하거나 sRGB image 를 하나도
판정하지 못하면 exit 1 이다.

  [1] sRGB image 의 DDS 는 R8G8B8A8_UNORM_SRGB 이고, mip 이 1x1 까지 있고, mip 0 이 source 와 bit 단위로 같다.
  [2] sRGB image 의 크기가 2 의 거듭제곱이면 mip k 와 encode(box2x2(decode(mip k-1))) 의 차이가 1 이하다.
  [3] linear image 의 DDS 는 R8G8B8A8_UNORM 이고 mip 0 이 source 와 bit 단위로 같다.
  [4] --before 를 주면: linear image 의 DDS 는 --before 의 DDS 와 format, 크기, mip 수, 모든 mip 의 pixel 이 같다.
  [5] 판정 없이 수치만: base color texture 의 Rec.709 luminance 를 모든 texel 에 걸쳐 평균한 값의 비.

JPEG 는 decoder 마다 값이 다를 수 있다 (T.81 은 IDCT 와 chroma upsampling 을 bit 단위로 정하지 않는다). Khronos Sponza 에서는
WIC 와 Pillow 가 bit 단위로 같았다 (측정).

받은 asset 에도 python -I 로 돌릴 수 있게 한다 (scripts/gltffiles.py).
"""

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gltffiles import file_in_gltf_folder
from imagefiles import DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, ImageFileError, read_dds_rgba8

REPO_ROOT = Path(__file__).resolve().parent.parent

SRGB_SLOTS = {'baseColor', 'emissive'}
LINEAR_SLOTS = {'metallicRoughness', 'normal', 'occlusion'}
REC709_LUMINANCE = np.array([0.2126, 0.7152, 0.0722])

PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'
PNG_COLOR_TYPE_RGB = 2
PNG_COLOR_TYPE_RGBA = 6
JPEG_SIGNATURE = b'\xff\xd8\xff'
JPEG_TRAITS = {'progressive': 'progressive', 'ICC': 'icc_profile', 'Exif': 'exif'}


def slots_of_material(material):
    pbr = material.get('pbrMetallicRoughness', {})
    texture_infos = {
        'baseColor': pbr.get('baseColorTexture'),
        'metallicRoughness': pbr.get('metallicRoughnessTexture'),
        'normal': material.get('normalTexture'),
        'occlusion': material.get('occlusionTexture'),
        'emissive': material.get('emissiveTexture'),
    }
    return {slot: info['index'] for slot, info in texture_infos.items() if info is not None}


def slots_by_uri(gltf):
    """uri 마다 그 image 를 쓰는 slot. loader 는 uri 가 가리키는 파일마다 DDS 하나를 만든다. uri 가 없는 image 는 None 에 모은다."""
    slots = {}
    for material in gltf.get('materials', []):
        for slot, texture in slots_of_material(material).items():
            image = gltf['textures'][texture].get('source')
            if image is not None:
                slots.setdefault(gltf['images'][image].get('uri'), set()).add(slot)
    return slots


def read_png_rgba8(path, header):
    if header[12:16] != b'IHDR':
        raise ImageFileError(f'{path} is not a PNG file')
    bit_depth, color_type = header[24], header[25]
    if bit_depth != 8 or color_type not in (PNG_COLOR_TYPE_RGB, PNG_COLOR_TYPE_RGBA):
        raise ImageFileError(f'{path} is {bit_depth} bit with color type {color_type}, not 8 bit RGB or RGBA')
    with Image.open(path) as image:
        return np.asarray(image.convert('RGBA'))


def read_jpeg_rgba8(path):
    with Image.open(path) as image:
        if image.mode != 'RGB':
            raise ImageFileError(f'{path} is a {image.mode} JPEG, not RGB')
        # glTF spec 은 JPEG 의 ICC profile 을 무시하고 (MUST) Exif 도 무시하라고 (SHOULD) 한다 (Images 절).
        # Pillow 는 둘을 적용하지 않고 info 에 담기만 하므로 개수만 센다.
        traits = {'JPEG'} | {trait for trait, key in JPEG_TRAITS.items() if image.info.get(key)}
        return np.asarray(image.convert('RGBA')), traits


def read_source_rgba8(path):
    """((height, width, 4) uint8 배열, source 의 형식과 JPEG_TRAITS 가운데 가진 것의 set)"""
    with open(path, 'rb') as file:
        header = file.read(26)
    if header.startswith(PNG_SIGNATURE):
        return read_png_rgba8(path, header), {'PNG'}
    if header.startswith(JPEG_SIGNATURE):
        return read_jpeg_rgba8(path)
    raise ImageFileError(f'{path} is neither a PNG nor a JPEG file')


def decode_srgb(encoded):
    value = encoded / 255.0
    return np.where(value <= 0.04045, value / 12.92, ((value + 0.055) / 1.055) ** 2.4)


def encode_srgb(linear):
    value = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.maximum(linear, 0.0) ** (1 / 2.4) - 0.055)
    return np.floor(np.clip(value, 0.0, 1.0) * 255.0 + 0.5)


def sampled_rgb(dxgi_format, texels):
    """GPU 가 이 format 의 texel 을 sample 할 때 읽는 linear RGB"""
    rgb = texels[..., :3]
    return decode_srgb(rgb) if dxgi_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB else rgb / 255.0


def box2x2(values):
    # 한 축의 길이가 1 이면 DirectXTex 의 box filter 처럼 그 축은 평균하지 않는다.
    if values.shape[0] > 1:
        values = (values[0::2] + values[1::2]) / 2
    if values.shape[1] > 1:
        values = (values[:, 0::2] + values[:, 1::2]) / 2
    return values


def is_power_of_two(size):
    return size & (size - 1) == 0


def problems_of_mip0(source, mip0):
    if mip0.shape != source.shape:
        return [f'mip 0 is {mip0.shape[1]}x{mip0.shape[0]} but the source is {source.shape[1]}x{source.shape[0]}']
    difference = np.abs(mip0.astype(np.int16) - source)
    differing = np.any(difference != 0, axis=2)
    if not differing.any():
        return []
    count = int(np.count_nonzero(differing))
    transparent = int(np.count_nonzero(differing & (source[..., 3] == 0)))
    return [f'mip 0 differs from the source in {count} texels by up to {int(difference.max())}, {transparent} of them at alpha 0'
            + (' (only at alpha 0)' if transparent == count else '')]


def problems_of_format(dxgi_format, expected):
    return [] if dxgi_format == expected else [f'DXGI format {dxgi_format}, not {expected}']


def problems_of_srgb_image(source, dxgi_format, mips):
    problems = problems_of_format(dxgi_format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
    full_mip_count = max(mips[0].shape[:2]).bit_length()
    if len(mips) != full_mip_count:
        problems.append(f'{len(mips)} mips, not {full_mip_count}')
    return problems + problems_of_mip0(source, mips[0])


def problems_of_linear_image(source, dxgi_format, mips):
    return problems_of_format(dxgi_format, DXGI_FORMAT_R8G8B8A8_UNORM) + problems_of_mip0(source, mips[0])


def srgb_mip_differences(mips):
    """mip 마다 (linear 공간 box 와의 최대 차이, gamma 공간 box 와의 RGB 최대 차이)"""
    differences = []
    for level in range(1, len(mips)):
        previous = mips[level - 1].astype(np.float64)
        current = mips[level].astype(np.float64)
        linear_box = np.concatenate([encode_srgb(box2x2(decode_srgb(previous[..., :3]))),
                                     np.floor(box2x2(previous[..., 3:]) + 0.5)], axis=2)
        gamma_box = np.floor(box2x2(previous[..., :3]) + 0.5)
        if current.shape != linear_box.shape:
            differences.append((np.inf, np.inf))
            continue
        differences.append((np.abs(current - linear_box).max(), np.abs(current[..., :3] - gamma_box).max()))
    return differences


def problems_against_before(dxgi_format, mips, before_format, before_mips):
    problems = []
    if dxgi_format != before_format:
        problems.append(f'DXGI format {dxgi_format}, before {before_format}')
    if len(mips) != len(before_mips):
        problems.append(f'{len(mips)} mips, before {len(before_mips)}')
    for level, (mip, before_mip) in enumerate(zip(mips, before_mips)):
        if mip.shape != before_mip.shape:
            problems.append(f'mip {level} is {mip.shape[1]}x{mip.shape[0]}, before {before_mip.shape[1]}x{before_mip.shape[0]}')
        elif (differing := int(np.count_nonzero(np.any(mip != before_mip, axis=2)))) > 0:
            problems.append(f'mip {level}: {differing} texels differ from before')
    return problems


class Tally:
    def __init__(self):
        self.counts = {}
        self.failures = []

    def judge(self, check, name, problems):
        passed, total = self.counts.get(check, (0, 0))
        self.counts[check] = (passed + (not problems), total + 1)
        self.failures += [f'[{check}] {name}: {problem}' for problem in problems]

    def total(self, check):
        return self.counts.get(check, (0, 0))[1]

    def summary(self, check):
        passed, total = self.counts.get(check, (0, 0))
        return f'{passed}/{total} PASS'


class LuminanceMeans:
    def __init__(self):
        self.sums = {}
        self.counts = {}

    def add(self, source, linear_rgb):
        luminance = linear_rgb @ REC709_LUMINANCE
        self.sums[source] = self.sums.get(source, 0.0) + float(luminance.sum())
        self.counts[source] = self.counts.get(source, 0) + luminance.size

    def ratio(self, numerator, denominator):
        return (self.sums[numerator] / self.counts[numerator]) / (self.sums[denominator] / self.counts[denominator])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('gltf', help='the .gltf whose textures the loader converted')
    parser.add_argument('--before', metavar='DIR',
                        help='a folder holding the DDS files from before the change at the same path relative to the repo root. '
                             'Without it [4] and the before ratio of [5] are skipped')
    args = parser.parse_args()

    gltf_path = Path(args.gltf).resolve()
    try:
        gltf = json.loads(gltf_path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        print(f'error: cannot read the glTF: {error}', file=sys.stderr)
        return 1
    if args.before and not gltf_path.is_relative_to(REPO_ROOT):
        parser.error('--before needs a glTF under the repo')
    before_dir = Path(args.before).resolve() / gltf_path.parent.relative_to(REPO_ROOT) if args.before else None

    slots_of_uri = slots_by_uri(gltf)
    srgb_images, linear_images, mixed_images = [], [], []
    for uri, slots in sorted(slots_of_uri.items(), key=lambda item: str(item[0])):
        if slots <= SRGB_SLOTS:
            srgb_images.append((uri, slots))
        elif slots <= LINEAR_SLOTS:
            linear_images.append((uri, slots))
        else:
            mixed_images.append(f'{uri} ({", ".join(sorted(slots))})')

    tally = Tally()
    means = LuminanceMeans()
    source_traits = Counter()
    base_color_textures = 0
    non_power_of_two = []
    largest_linear_box_difference = largest_gamma_box_difference = 0.0

    for uri, slots in srgb_images + linear_images:
        if uri is None:
            tally.judge('read', 'images without uri', ['the loader only converts images with a uri'])
            continue
        try:
            source_path = file_in_gltf_folder(gltf_path, uri)
            source, traits = read_source_rgba8(source_path)
        except (OSError, ValueError, ImageFileError) as error:
            tally.judge('read', uri, [str(error)])
            continue
        source_traits.update(traits)
        # DDS 의 경로 규칙은 Model/Loader/TextureConvert.cpp 의 CompileTextureOnDemand 가 정한다.
        dds_path = source_path.with_suffix('.dds')
        try:
            dxgi_format, mips = read_dds_rgba8(dds_path)
            before_format, before_mips = (read_dds_rgba8(before_dir / dds_path.relative_to(gltf_path.parent))
                                          if before_dir else (None, None))
        except (OSError, ImageFileError) as error:
            tally.judge('read', uri, [str(error)])
            continue

        if slots <= LINEAR_SLOTS:
            tally.judge(3, uri, problems_of_linear_image(source, dxgi_format, mips))
            if before_dir:
                tally.judge(4, uri, problems_against_before(dxgi_format, mips, before_format, before_mips))
            continue

        tally.judge(1, uri, problems_of_srgb_image(source, dxgi_format, mips))

        height, width = mips[0].shape[:2]
        if is_power_of_two(width) and is_power_of_two(height):
            differences = srgb_mip_differences(mips)
            largest_linear_box_difference = max([largest_linear_box_difference] + [linear for linear, _ in differences])
            largest_gamma_box_difference = max([largest_gamma_box_difference] + [gamma for _, gamma in differences])
            tally.judge(2, uri, [f'mip {level}: |difference| {linear:g} from the linear-space box'
                                 for level, (linear, _) in enumerate(differences, start=1) if linear > 1])
        else:
            non_power_of_two.append(f'{uri} ({width}x{height})')

        if 'baseColor' in slots:
            base_color_textures += 1
            means.add('source as linear', source[..., :3] / 255.0)
            means.add('source decoded', decode_srgb(source[..., :3]))
            means.add('new', sampled_rgb(dxgi_format, mips[0]))
            if before_dir:
                means.add('before', sampled_rgb(before_format, before_mips[0]))

    # glTF 나 texture 를 잘못 가리켜 판정한 것이 없으면 통과로 보지 않는다.
    if tally.total(1) + tally.total(3) == 0:
        tally.failures.append('no image was checked')
    elif tally.total(1) == 0:
        tally.failures.append('no srgb image was checked')

    unused_uris = {image.get('uri') for image in gltf.get('images', [])} - slots_of_uri.keys()
    print(f'glTF: {gltf_path}')
    print(f'images by uri: {len(srgb_images)} srgb, {len(linear_images)} linear, {len(mixed_images)} used by both (not checked), '
          f'{len(unused_uris)} unused')
    for image in mixed_images:
        print(f'  used by both: {image}')
    jpeg_traits = ', '.join(f'{trait} {source_traits[trait]}' for trait in JPEG_TRAITS)
    print(f'sources: {source_traits["JPEG"]} JPEG ({jpeg_traits}), {source_traits["PNG"]} PNG')
    print(f'before: {before_dir}' if before_dir else 'before: not given')
    print()
    if tally.total('read'):
        print(f'[read] {tally.total("read")} images could not be read and are left out of the checks')
    print(f'[1] srgb image is R8G8B8A8_UNORM_SRGB with a full mip chain and mip 0 == source: {tally.summary(1)}')
    print(f'[2] srgb mip k within 1 of encode(box2x2(decode(mip k-1))): {tally.summary(2)}, '
          f'{len(non_power_of_two)} skipped as not a power of two')
    for image in non_power_of_two:
        print(f'  skipped: {image}')
    print(f'    largest |difference| over all mips: {largest_linear_box_difference:g} from the linear-space box, '
          f'{largest_gamma_box_difference:g} from the gamma-space box')
    print(f'[3] linear image is R8G8B8A8_UNORM and mip 0 == source: {tally.summary(3)}')
    print('[4] linear image == before (format, size, mip count, pixels of every mip): '
          + (tally.summary(4) if before_dir else 'skipped, --before not given'))
    print(f'[5] base color albedo: mean Rec.709 luminance over all texels of {base_color_textures} textures, alpha ignored')
    if base_color_textures:
        print(f'    new DDS as sampled / source read as linear: {means.ratio("new", "source as linear"):.4f}')
        print(f'    source decoded as sRGB / source read as linear: {means.ratio("source decoded", "source as linear"):.4f} '
              '(the first ratio when every mip 0 == source)')
        print('    new DDS as sampled / before DDS as sampled: '
              + (f'{means.ratio("new", "before"):.4f}' if before_dir else 'skipped, --before not given'))

    if tally.failures:
        print()
        print(f'{len(tally.failures)} failures')
        for failure in tally.failures:
            print(f'  {failure}')
    return 1 if tally.failures else 0


if __name__ == '__main__':
    sys.exit(main())
