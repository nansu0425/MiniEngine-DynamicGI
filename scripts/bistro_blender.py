import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fbxscene_blender import export_scene, import_fbx, read_arguments, start_empty_scene

USAGE = 'blender -b --factory-startup --python scripts/bistro_blender.py -- <Exterior FBX> <Interior FBX> <glTF> <description JSON>'

EXTERIOR_ROOT_NAME = 'BistroExterior'
CENTIMETRES_TO_METRES = 0.01
INTERIOR_COPY_OF_EXTERIOR_PREFIX = 'Bistro_Research_Exterior_'
STOREFRONT_GLASS_MATERIAL = 'MASTER_Glass_Exterior'


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
    remove(copies_of_exterior + storefront_glass)
    return f'removed {len(copies_of_exterior)} interior copies of exterior meshes and {len(storefront_glass)} storefront glass meshes'


def main():
    exterior_fbx, interior_fbx, gltf, description_json = read_arguments(USAGE, 4)
    start_empty_scene()
    exterior = import_fbx(exterior_fbx)
    bpy.data.objects[EXTERIOR_ROOT_NAME].scale = (CENTIMETRES_TO_METRES,) * 3
    interior = import_fbx(interior_fbx)
    bpy.context.view_layer.update()
    report = merge_interior_into_exterior(exterior, interior)
    export_scene(gltf, description_json, [report])


main()
