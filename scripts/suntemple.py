import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fbxscene

USAGE = 'python -I scripts/suntemple.py <Sun Temple folder> <scene file>'

SUN_TEMPLE = fbxscene.FbxScene(
    blender_script=Path(__file__).resolve().parent / 'suntemple_blender.py',
    fbx_names=['SunTemple.fbx'],
    generator='scripts/suntemple.py',
    temporary_folder_prefix='suntemple-',
    emissive_strength=1000.0,
)


if __name__ == '__main__':
    sys.exit(fbxscene.main(SUN_TEMPLE, USAGE))
