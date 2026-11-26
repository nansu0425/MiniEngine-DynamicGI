import json
import sys
from pathlib import Path

import addon_utils
import bpy

USAGE = 'blender -b --factory-startup --python scripts/bistro_blender.py -- <FBX> <glTF> <material JSON>'

FBX_LIGHT_IMPORT_BREAKING_ADDONS = ['cycles']

EXTERIOR_ROOT_NAME = 'BistroExterior'
CENTIMETRES_TO_METRES = 0.01

PRINCIPLED_TEXTURE_INPUTS = {
    'Base Color': 'baseColor',
    'Specular IOR Level': 'specular',
    'Emission Color': 'emissive',
}


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


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if len(arguments) != 3:
        print(USAGE, file=sys.stderr)
        sys.exit(2)
    fbx, gltf, material_json = arguments
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for addon in FBX_LIGHT_IMPORT_BREAKING_ADDONS:
        addon_utils.disable(addon)
    bpy.ops.import_scene.fbx(filepath=fbx)
    bpy.data.objects[EXTERIOR_ROOT_NAME].scale = (CENTIMETRES_TO_METRES,) * 3

    materials = {material.name: describe_material(material) for material in bpy.data.materials if material.node_tree is not None}
    Path(material_json).write_text(json.dumps(materials, indent=1), encoding='utf-8')

    bpy.ops.export_scene.gltf(filepath=gltf, export_format='GLTF_SEPARATE', export_image_format='NONE', export_materials='EXPORT',
                              export_tangents=True, export_vertex_color='NONE', export_cameras=False, export_lights=False,
                              export_animations=False, export_yup=True)


main()
