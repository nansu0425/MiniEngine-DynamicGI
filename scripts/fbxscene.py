import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer import scene_file_gltf_path

FETCHED_BLENDER_FOLDER = Path(__file__).resolve().parent.parent / 'Tools' / 'Blender'
SOURCE_TEXTURE_FOLDER = 'Textures'
WRITTEN_TEXTURE_FOLDER = 'Textures'
DOUBLE_SIDED_MATERIAL_SUFFIX = '.DoubleSided'
BLENDER_DUPLICATE_NAME_SUFFIX = re.compile(r'\.\d{3}$')
OPAQUE_ALPHA = 255


class ConversionError(Exception):
    pass


@dataclass
class FbxScene:
    blender_script: Path
    fbx_names: list
    generator: str
    temporary_folder_prefix: str


@dataclass
class BlenderExport:
    gltf: Path
    materials: dict
    reports: list


def find_blender():
    if 'BLENDER' in os.environ:
        blender = Path(os.environ['BLENDER'])
        if not blender.is_file():
            raise ConversionError(f'cannot find Blender at {blender}, which BLENDER names')
        return blender
    fetched = list(FETCHED_BLENDER_FOLDER.glob('*/blender.exe'))
    if len(fetched) != 1:
        raise ConversionError(f'cannot find one blender.exe in {FETCHED_BLENDER_FOLDER}. Run scripts/fetch-assets.ps1 or set BLENDER to blender.exe')
    return fetched[0]


def source_file(source_folder, name):
    path = source_folder / name
    if not path.is_file():
        raise ConversionError(f'cannot find {path}')
    return path


def export_with_blender(scene, source_folder, folder):
    gltf, description_json = folder / 'exported.gltf', folder / 'description.json'
    fbx = [source_file(source_folder, name) for name in scene.fbx_names]
    completed = subprocess.run([str(find_blender()), '-b', '--factory-startup', '--python', str(scene.blender_script), '--', *map(str, fbx), str(gltf), str(description_json)],
                               capture_output=True, text=True, encoding='utf-8', errors='replace')
    if completed.returncode != 0 or not gltf.is_file() or not description_json.is_file():
        raise ConversionError(f'Blender failed with exit code {completed.returncode}\n{completed.stdout[-4000:]}\n{completed.stderr[-4000:]}')
    description = json.loads(description_json.read_text(encoding='utf-8'))
    return BlenderExport(gltf, description['materials'], description['reports'])


def decode_dds(path):
    import numpy as np
    from PIL import Image
    with Image.open(path) as image:
        return np.asarray(image.convert('RGBA'))


def has_cutout(rgba):
    return bool((rgba[..., 3] < OPAQUE_ALPHA).any())


def opengl_normal_from_directx_bc5(rgba):
    import numpy as np
    x = rgba[..., 0].astype(np.float32) / 255.0 * 2.0 - 1.0
    y = rgba[..., 1].astype(np.float32) / 255.0 * 2.0 - 1.0
    z = np.sqrt(np.clip(1.0 - x * x - y * y, 0.0, 1.0))
    encoded = np.stack([x, -y, z], axis=-1) * 0.5 + 0.5
    return np.clip(np.rint(encoded * 255.0), 0, 255).astype(np.uint8)


def write_png(pixels, path):
    from PIL import Image
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(path.name + '.partial')
    Image.fromarray(pixels).save(partial, format='PNG')
    partial.replace(path)


class TextureWriter:
    def __init__(self, source_folder, gltf_folder):
        self.source_folder = source_folder
        self.gltf_folder = gltf_folder
        self.images = []
        self.textures = []
        self.texture_indices = {}

    def source(self, name):
        path = self.source_folder / name
        if not path.is_file():
            raise ConversionError(f'cannot find the texture {path}')
        return path

    def add(self, name, pixels):
        if name not in self.texture_indices:
            uri = f'{WRITTEN_TEXTURE_FOLDER}/{Path(name).stem}.png'
            write_png(pixels, self.gltf_folder / uri)
            self.images.append({'uri': uri})
            self.textures.append({'source': len(self.images) - 1})
            self.texture_indices[name] = len(self.textures) - 1
        return {'index': self.texture_indices[name]}

    def base_color(self, name):
        rgba = decode_dds(self.source(name))
        cutout = has_cutout(rgba)
        return self.add(name, rgba if cutout else rgba[..., :3]), cutout

    def color(self, name):
        return self.add(name, decode_dds(self.source(name))[..., :3])

    def normal(self, name):
        return self.add(name, opengl_normal_from_directx_bc5(decode_dds(self.source(name))))


def gltf_material(name, described, textures):
    pbr = {}
    material = {'name': name, 'pbrMetallicRoughness': pbr}
    if described.get('baseColor'):
        pbr['baseColorTexture'], cutout = textures.base_color(described['baseColor'])
        if cutout:
            material['alphaMode'] = 'MASK'
    else:
        pbr['baseColorFactor'] = described['baseColorValue']
    if described.get('specular'):
        pbr['metallicRoughnessTexture'] = textures.color(described['specular'])
    else:
        pbr['metallicFactor'] = 0.0
        pbr['roughnessFactor'] = described['roughnessValue']
    if described.get('normal'):
        material['normalTexture'] = textures.normal(described['normal'])
    if described.get('emissive'):
        material['emissiveTexture'] = textures.color(described['emissive'])
        material['emissiveFactor'] = [described['emissionStrength']] * 3
    elif any(value > 0.0 for value in described['emissiveValue']):
        material['emissiveFactor'] = [value * described['emissionStrength'] for value in described['emissiveValue']]
    if BLENDER_DUPLICATE_NAME_SUFFIX.sub('', name).endswith(DOUBLE_SIDED_MATERIAL_SUFFIX):
        material['doubleSided'] = True
    return material


def rewrite_materials(gltf, materials, textures, generator):
    rewritten = []
    for material in gltf['materials']:
        if material['name'] not in materials:
            raise ConversionError(f'Blender did not describe the material {material["name"]}')
        rewritten.append(gltf_material(material['name'], materials[material['name']], textures))
    gltf['materials'] = rewritten
    gltf['images'] = textures.images
    gltf['textures'] = textures.textures
    for unused in ('extensionsUsed', 'extensionsRequired', 'samplers'):
        gltf.pop(unused, None)
    gltf['asset']['generator'] = generator


def replace_if_changed(source, destination):
    if destination.exists() and destination.read_bytes() == source.read_bytes():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def convert(scene, source_folder, gltf_path):
    source_folder = Path(source_folder).resolve()
    with tempfile.TemporaryDirectory(prefix=scene.temporary_folder_prefix) as folder:
        exported = export_with_blender(scene, source_folder, Path(folder))
        for report in exported.reports:
            print(report)
        gltf = json.loads(exported.gltf.read_text(encoding='utf-8'))
        rewrite_materials(gltf, exported.materials, TextureWriter(source_folder / SOURCE_TEXTURE_FOLDER, gltf_path.parent), scene.generator)
        bin_name = gltf_path.with_suffix('.bin').name
        exported_bin = exported.gltf.parent / gltf['buffers'][0]['uri']
        gltf['buffers'][0]['uri'] = bin_name
        rewritten = exported.gltf.with_name('rewritten.gltf')
        rewritten.write_text(json.dumps(gltf, indent=1), encoding='utf-8')
        replace_if_changed(exported_bin, gltf_path.with_name(bin_name))
        replace_if_changed(rewritten, gltf_path)


def main(scene, usage):
    if len(sys.argv) != 3:
        print(usage, file=sys.stderr)
        return 2
    try:
        gltf_path = scene_file_gltf_path(sys.argv[2])
        convert(scene, sys.argv[1], gltf_path)
        print(f'glTF: {gltf_path}')
    except (ConversionError, OSError, ValueError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0
