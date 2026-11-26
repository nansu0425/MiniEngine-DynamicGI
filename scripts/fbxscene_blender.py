import json
import sys
from pathlib import Path

import addon_utils
import bpy

FBX_LIGHT_IMPORT_BREAKING_ADDONS = ['cycles']

PRINCIPLED_TEXTURE_INPUTS = {
    'Base Color': 'baseColor',
    'Specular IOR Level': 'specular',
    'Emission Color': 'emissive',
}


def read_arguments(usage, count):
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if len(arguments) != count:
        print(usage, file=sys.stderr)
        sys.exit(2)
    return arguments


def start_empty_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for addon in FBX_LIGHT_IMPORT_BREAKING_ADDONS:
        addon_utils.disable(addon)


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def linked_image_name(socket):
    for link in socket.links:
        node = link.from_node
        if node.type == 'TEX_IMAGE' and node.image is not None:
            return Path(bpy.path.abspath(node.image.filepath)).name
    return None


def normal_map_image_name(bsdf):
    for link in bsdf.inputs['Normal'].links:
        if link.from_node.type == 'NORMAL_MAP':
            return linked_image_name(link.from_node.inputs['Color'])
    return None


def describe_material(material):
    bsdf = next((node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED'), None)
    if bsdf is None:
        return {}
    described = {key: linked_image_name(bsdf.inputs[name]) for name, key in PRINCIPLED_TEXTURE_INPUTS.items()}
    described['normal'] = normal_map_image_name(bsdf)
    described['baseColorValue'] = list(bsdf.inputs['Base Color'].default_value)
    described['roughnessValue'] = bsdf.inputs['Roughness'].default_value
    described['emissiveValue'] = list(bsdf.inputs['Emission Color'].default_value)[:3]
    described['emissionStrength'] = bsdf.inputs['Emission Strength'].default_value
    return described


def export_scene(gltf, description_json, reports):
    materials = {material.name: describe_material(material) for material in bpy.data.materials if material.node_tree is not None}
    Path(description_json).write_text(json.dumps({'materials': materials, 'reports': reports}, indent=1), encoding='utf-8')

    bpy.ops.export_scene.gltf(filepath=gltf, export_format='GLTF_SEPARATE', export_image_format='NONE', export_materials='EXPORT',
                              export_tangents=True, export_vertex_color='NONE', export_cameras=False, export_lights=False,
                              export_animations=False, export_yup=True)
