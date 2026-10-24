"""glTF 의 uri 를 그 glTF 폴더 안의 파일 경로로 바꾼다.

받은 asset 을 읽는 script 가 같은 규칙으로 uri 를 거부하도록 여기에 모은다. 그 script 는 받은 asset 에도 python -I 로
돌릴 수 있도록, cwd 나 asset 폴더는 빼고 자기 폴더만 sys.path 에 넣어 이 module 을 import 한다.
"""

from pathlib import PurePosixPath, PureWindowsPath
from urllib.parse import urlsplit


def file_in_gltf_folder(gltf_path, uri):
    """loader 처럼 uri 를 glTF 폴더 기준의 경로로 읽되, glTF 폴더 밖의 파일은 가리키지 못하게 한다."""
    if not isinstance(uri, str) or not uri:
        raise ValueError(f'uri {uri!r} is not a file name')
    if urlsplit(uri).scheme:
        raise ValueError(f'uri {uri!r} has a scheme. Only paths relative to the glTF folder are read')
    # UNC 경로는 resolve 만 해도 원격에 접속하므로 경로를 만들기 전에 거부한다.
    if PureWindowsPath(uri).anchor or PurePosixPath(uri).is_absolute():
        raise ValueError(f'uri {uri!r} is an absolute or UNC path')
    folder = gltf_path.parent.resolve()
    path = (folder / uri).resolve()
    if not path.is_relative_to(folder):
        raise ValueError(f'uri {uri!r} points outside the glTF folder')
    return path
