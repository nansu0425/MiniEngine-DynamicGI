import json
import sys
from pathlib import Path

import addon_utils
import bpy
from mathutils import Vector

USAGE = 'blender -b --factory-startup --python scripts/bistro_blender.py -- <Exterior FBX> <Interior FBX> <glTF> <material JSON>'

FBX_LIGHT_IMPORT_BREAKING_ADDONS = ['cycles']

EXTERIOR_ROOT_NAME = 'BistroExterior'
CENTIMETRES_TO_METRES = 0.01
INTERIOR_COPY_OF_EXTERIOR_PREFIX = 'Bistro_Research_Exterior_'
STOREFRONT_GLASS_MATERIAL = 'MASTER_Glass_Exterior'

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


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def world_bounds(objects):
    corners = [o.matrix_world @ Vector(corner) for o in objects for corner in o.bound_box]
    return Vector([min(c[i] for c in corners) for i in range(3)]), Vector([max(c[i] for c in corners) for i in range(3)])


def overlaps(a, b):
    return all(a[0][i] < b[1][i] and b[0][i] < a[1][i] for i in range(3))


def uses_material(o, name):
    return any(slot.material is not None and slot.material.name == name for slot in o.material_slots)


def remove(objects):
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)


def merge_interior_into_exterior(exterior, interior):
    interior_meshes = [o for o in interior if o.type == 'MESH']
    copies_of_exterior = [o for o in interior_meshes if o.name.startswith(INTERIOR_COPY_OF_EXTERIOR_PREFIX)]
    interior_bounds = world_bounds([o for o in interior_meshes if o not in copies_of_exterior])
    storefront_glass = [o for o in exterior if o.type == 'MESH' and uses_material(o, STOREFRONT_GLASS_MATERIAL) and overlaps(world_bounds([o]), interior_bounds)]
    print(f'removed {len(copies_of_exterior)} interior copies of exterior meshes and {len(storefront_glass)} storefront glass meshes')
    remove(copies_of_exterior + storefront_glass)


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if len(arguments) != 4:
        print(USAGE, file=sys.stderr)
        sys.exit(2)
    exterior_fbx, interior_fbx, gltf, material_json = arguments
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for addon in FBX_LIGHT_IMPORT_BREAKING_ADDONS:
        addon_utils.disable(addon)
    exterior = import_fbx(exterior_fbx)
    bpy.data.objects[EXTERIOR_ROOT_NAME].scale = (CENTIMETRES_TO_METRES,) * 3
    interior = import_fbx(interior_fbx)
    bpy.context.view_layer.update()
    merge_interior_into_exterior(exterior, interior)

    materials = {material.name: describe_material(material) for material in bpy.data.materials if material.node_tree is not None}
    Path(material_json).write_text(json.dumps(materials, indent=1), encoding='utf-8')

    bpy.ops.export_scene.gltf(filepath=gltf, export_format='GLTF_SEPARATE', export_image_format='NONE', export_materials='EXPORT',
                              export_tangents=True, export_vertex_color='NONE', export_cameras=False, export_lights=False,
                              export_animations=False, export_yup=True)


main()
