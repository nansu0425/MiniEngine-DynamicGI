import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fbxscene

USAGE = 'python -I scripts/bistro.py <Bistro folder> <scene file>'

BISTRO = fbxscene.FbxScene(
    blender_script=Path(__file__).resolve().parent / 'bistro_blender.py',
    fbx_names=['BistroExterior.fbx', 'BistroInterior.fbx'],
    generator='scripts/bistro.py',
    temporary_folder_prefix='bistro-',
)


if __name__ == '__main__':
    sys.exit(fbxscene.main(BISTRO, USAGE))
