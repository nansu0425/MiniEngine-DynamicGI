import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fbxscene_blender import export_scene, import_fbx, read_arguments, start_empty_scene

USAGE = 'blender -b --factory-startup --python scripts/suntemple_blender.py -- <FBX> <glTF> <description JSON>'


def main():
    fbx, gltf, description_json = read_arguments(USAGE, 3)
    start_empty_scene()
    import_fbx(fbx)
    export_scene(gltf, description_json, [])


main()
