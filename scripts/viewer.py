"""ModelViewer 를 build 하고 띄워서 control channel 로 조작하고 확인하는 harness.

app 명령의 사용법은 `do help` 가 app 에서 받아 온다. app flag 는 MiniEngine/readme.md 에 있다.
"""

import argparse
import contextlib
import copy
import ctypes
import functools
import hashlib
import json
import msvcrt
import os
import posixpath
import re
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

from imagefiles import ImageFileError, read_dds_rgba32f
from pickanswers import camera_ray, grid_cells, is_unambiguous, vector_of

REPO_ROOT = Path(__file__).resolve().parent.parent
ENGINE_DIR = REPO_ROOT / 'MiniEngine'
VIEWER_DIR = ENGINE_DIR / 'ModelViewer'
HARNESS_DIR = VIEWER_DIR / 'Harness'
RUNS_DIR = HARNESS_DIR / 'runs'
INSTANCE_FILE = HARNESS_DIR / 'instance.json'
BASELINE_DIR = REPO_ROOT / 'scripts' / 'harness-baseline'
USER_SETTINGS = [VIEWER_DIR / 'editor.ini', VIEWER_DIR / 'imgui.ini']
MODEL_VIEWER_SOURCE = VIEWER_DIR / 'ModelViewer.cpp'
# repo 가 갖는 scene file 의 폴더. 그 glTF 는 scripts/fetch-assets.ps1 이 받거나 만든다.
SCENES_DIR = VIEWER_DIR / 'Scenes'
VALIDATION_SHADER = ENGINE_DIR / 'Model' / 'Raytracing' / 'RayQueryValidationCS.hlsl'
# selftest 가 pickscene.py 의 test scene 을 쓰는 HARNESS_DIR 안의 폴더
PICK_SCENE = 'pick-known-answers'
# selftest 가 run 과 같은 방식으로 실행하고 이름을 붙인 응답으로 판정하는, 완료 판정 기준의 acceptance scenario
ACCEPTANCE_SCENARIO = REPO_ROOT / 'scripts' / 'harness-scenarios' / 'acceptance.txt'
# run 의 줄 형식. --help 가 사용법을 보인다.
SCENARIO_NAMED_LINE = re.compile(r'^(\w+) = (.+)$')
SCENARIO_REFERENCE = re.compile(r'\{(\w+(?:\.[^.{}]+)*)\}')
D3D12_MESSAGE_LOG_HEADER = ENGINE_DIR / 'Core' / 'Automation' / 'D3D12MessageLog.h'
# D3D12 message ID 의 이름을 읽을 Agility SDK 의 version 이 여기에 있다.
PACKAGES_CONFIG = ENGINE_DIR / 'Core' / 'packages.config'
SOURCE_DIRS = [ENGINE_DIR / 'Core', ENGINE_DIR / 'Model']
SOURCE_SUFFIXES = {'.cpp', '.h', '.inl', '.hlsl', '.hlsli'}
# source 위치가 있는 MSBuild error 줄. DXC 를 부르는 FxCompile 은 source 를 project 폴더 기준 상대 경로로 적는다.
MSBUILD_ERROR_WITH_SOURCE = re.compile(r'^(?P<source>[^(:]+)(?P<rest>\([\d,-]+\): .*\[(?P<project>[^\]]+\.vcxproj)\])$')

CONFIGS = ['Debug', 'Profile', 'Release']
# 기능 확인은 debug layer 가 켜지는 Debug 로 한다. 시간은 Profile 에서 잰다.
DEFAULT_CONFIG = 'Debug'

# 처음 load 할 때 .mini 와 DDS cache 를 만드므로 길게 둔다.
STARTUP_TIMEOUT = 600
COMMAND_TIMEOUT = 120
EXIT_TIMEOUT = 30
# frame 이 이 시간 동안 오르지 않으면 main loop 나 GPU 가 멈춘 것으로 본다.
HANG_SECONDS = 10
# camera 나 설정을 바꾼 뒤 수치를 비교할 capture 까지 기다리는 frame 수. 근거는 .claude/skills/modelviewer/SKILL.md 의 결정성 절에 있다.
SETTLE_FRAMES = 1
# start 가 남겨 두는 run 폴더 수. 실행 중인 instance 의 run 은 이와 관계없이 남긴다.
KEPT_RUNS = 10
PREVIEW_MAX_WIDTH = 1280
# scene capture 의 보기용 PNG 를 만드는 고정 exposure. 보기용이라 수치 판정에는 쓰지 않는다.
PREVIEW_EXPOSURE = 1.0
# Validation view 를 끈 뒤 수치를 비교할 scene capture 까지 기다리는 frame 수. 근거는 .claude/skills/modelviewer/SKILL.md 의 결정성 절에 있다.
VALIDATION_VIEW_SETTLE_FRAMES = 10
# 아무것도 바꾸지 않아도 frame 마다 바뀌는 state 의 값. state 끼리 비교할 때 뺀다.
TIME_VARYING_STATE = [('frame',), ('pt', 'samples'), ('pt', 'seconds')]


class HarnessError(Exception):
    pass


class AppExited(HarnessError):
    pass


class AppHung(HarnessError):
    def __init__(self, frame):
        super().__init__(f'frame stuck at {frame} for {HANG_SECONDS} s')
        self.frame = frame


# --- Windows process와 pipe ---------------------------------------------------------------

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.WaitForSingleObject.restype = wintypes.DWORD
kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
kernel32.PeekNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                                   ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD)]

PROCESS_TERMINATE = 0x0001
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
SYNCHRONIZE = 0x00100000
WAIT_OBJECT_0 = 0
STILL_ACTIVE = 259


def process_image(pid):
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, False, pid)
    if not handle:
        return None
    try:
        if kernel32.WaitForSingleObject(handle, 0) == WAIT_OBJECT_0:
            return None
        size = wintypes.DWORD(1024)
        buffer = ctypes.create_unicode_buffer(size.value)
        if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            return None
        return buffer.value
    finally:
        kernel32.CloseHandle(handle)


def is_running(instance):
    # pid 가 다른 process 에 다시 쓰였을 수 있으므로 exe 경로까지 본다.
    image = process_image(instance['pid'])
    return image is not None and os.path.normcase(image) == os.path.normcase(instance['exe'])


def wait_for_exit(pid, timeout):
    handle = kernel32.OpenProcess(SYNCHRONIZE, False, pid)
    if not handle:
        return True
    try:
        return kernel32.WaitForSingleObject(handle, int(timeout * 1000)) == WAIT_OBJECT_0
    finally:
        kernel32.CloseHandle(handle)


def kill(pid):
    handle = kernel32.OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, False, pid)
    if not handle:
        return
    try:
        kernel32.TerminateProcess(handle, 1)
        kernel32.WaitForSingleObject(handle, 10000)
    finally:
        kernel32.CloseHandle(handle)


class Channel:
    """control channel 연결 하나. 요청 한 줄에 응답 한 줄이 온다."""

    def __init__(self, pipe, timeout, alive=lambda: True):
        path = rf'\\.\pipe\{pipe}'
        deadline = time.monotonic() + timeout
        while True:
            try:
                self._file = open(path, 'r+b', buffering=0)
                break
            except OSError:
                if time.monotonic() > deadline or not alive():
                    raise HarnessError(f'cannot connect to {path}')
                time.sleep(0.1)
        self._handle = msvcrt.get_osfhandle(self._file.fileno())
        self._pending = b''

    def close(self):
        self._file.close()

    def send(self, line):
        try:
            self._file.write(line.encode('utf-8') + b'\n')
        except OSError as error:
            raise AppExited(f'the pipe closed: {error}')

    def receive(self, timeout):
        """응답 한 줄을 dict 로 돌려준다. timeout 안에 오지 않으면 None 이다."""
        deadline = time.monotonic() + timeout
        while b'\n' not in self._pending:
            available = self._available()
            if available:
                self._pending += self._file.read(available)
            elif time.monotonic() > deadline:
                return None
            else:
                time.sleep(0.005)
        line, _, self._pending = self._pending.partition(b'\n')
        # pose 의 %.9g 처럼 app 이 쓴 숫자의 자릿수를 그대로 비교할 수 있게 실수는 문자열로 둔다.
        return json.loads(line.decode('utf-8'), parse_float=str)

    def _available(self):
        available = wintypes.DWORD(0)
        if not kernel32.PeekNamedPipe(self._handle, None, 0, None, ctypes.byref(available), None):
            raise AppExited('the pipe closed')
        return available.value


class Session:
    """명령을 보내는 연결과, 응답을 기다리는 동안 hang 을 보는 ping 연결."""

    def __init__(self, instance):
        self.instance = instance
        self._commands = Channel(instance['pipe'], 10, lambda: is_running(instance))
        self._watchdog = None
        # 연결 중에 app 이 끝나면 exit code 를 읽을 수 있게 process handle 을 쥐고 있는다. handle 이 모두 닫히면 exit code 도 사라진다.
        self._process = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, False, instance['pid'])

    def close(self):
        self._commands.close()
        if self._watchdog:
            self._watchdog.close()
        if self._process:
            kernel32.CloseHandle(self._process)
            self._process = None

    def exit_code(self):
        """app 이 끝났으면 그 exit code, 아직 돌거나 알 수 없으면 None."""
        code = wintypes.DWORD()
        if not self._process or not kernel32.GetExitCodeProcess(self._process, ctypes.byref(code)) or code.value == STILL_ACTIVE:
            return None
        return code.value

    def ask(self, line, timeout=COMMAND_TIMEOUT):
        self._commands.send(line)
        deadline = time.monotonic() + timeout
        last_frame, last_change = None, time.monotonic()
        while True:
            response = self._commands.receive(1.0)
            if response is not None:
                return response
            if not is_running(self.instance):
                raise AppExited('the app exited')
            frame, now = self._ping(), time.monotonic()
            if frame is None or frame != last_frame:
                last_frame, last_change = frame, now
            elif now - last_change >= HANG_SECONDS:
                raise AppHung(frame)
            if now > deadline:
                raise HarnessError(f'no response to "{line}" in {timeout} s')

    def ask_ok(self, line, timeout=COMMAND_TIMEOUT):
        response = self.ask(line, timeout)
        if not response['ok']:
            raise HarnessError(f'{line}: {response["error"]}')
        return response

    def _ping(self):
        try:
            if self._watchdog is None:
                self._watchdog = Channel(self.instance['pipe'], 2)
            self._watchdog.send('ping')
            response = self._watchdog.receive(2.0)
            return response['frame'] if response else None
        except HarnessError:
            return None


# --- instance -----------------------------------------------------------------------------

def exe_path(config):
    return ENGINE_DIR / 'Build' / 'x64' / config / 'Output' / 'ModelViewer' / 'ModelViewer.exe'


def load_instance():
    if not INSTANCE_FILE.exists():
        return None
    return json.loads(INSTANCE_FILE.read_text(encoding='utf-8'))


def require_running():
    instance = load_instance()
    if instance is None:
        raise HarnessError('no harness instance. Run start first.')
    if not is_running(instance):
        raise HarnessError(describe_ended(instance))
    return instance


@functools.cache
def d3d12_message_names():
    """D3D12_MESSAGE_ID 의 숫자에서 이름으로. build 가 쓰는 Agility SDK 의 header 에서 읽는다.

    header 를 찾지 못해도 요약은 끝까지 찍어야 하므로 경고만 하고 이름 없이 진행한다.
    """
    def without_names(reason):
        print(f'warning: D3D12 message names are unknown because {reason}', file=sys.stderr)
        return {}

    try:
        packages = PACKAGES_CONFIG.read_text(encoding='utf-8-sig')
    except OSError as error:
        return without_names(f'{PACKAGES_CONFIG} cannot be read: {error}')
    version = re.search(r'id="Microsoft\.Direct3D\.D3D12" version="([^"]+)"', packages)
    if version is None:
        return without_names(f'{PACKAGES_CONFIG} has no Microsoft.Direct3D.D3D12 version')
    header = ENGINE_DIR / 'Packages' / f'Microsoft.Direct3D.D3D12.{version[1]}' / 'build' / 'native' / 'include' / 'd3d12sdklayers.h'
    if not header.exists():
        return without_names(f'{header} does not exist')
    text = header.read_text(encoding='utf-8', errors='replace')
    return {int(number): name for name, number in re.findall(r'D3D12_MESSAGE_ID_(\w+)\s*=\s*(\d+),', text)}


def logged_lines_per_d3d12_id():
    match = re.search(r'^\s*constexpr uint64_t kLoggedPerId = (\d+);', D3D12_MESSAGE_LOG_HEADER.read_text(encoding='utf-8'), re.M)
    if match is None:
        raise HarnessError(f'{D3D12_MESSAGE_LOG_HEADER} has no line constexpr uint64_t kLoggedPerId = <number>;')
    return int(match[1])


def summarize_d3d12_messages(text):
    """줄의 형식은 D3D12_MESSAGE_LOG_HEADER 에 있다."""
    status = re.search(r'^D3D12 messages: (.*)$', text, re.MULTILINE)
    exit_report = re.search(r'^D3D12 live objects at exit:$', text, re.MULTILINE)
    totals = {int(id): int(total) for id, total in re.findall(r'^D3D12 #(\d+) total=(\d+)$', text, re.MULTILINE)}
    logged_times = {int(id): int(times) for id, times in
                    re.findall(r'^D3D12 #(\d+): logged (\d+) times, the rest are only counted$', text, re.MULTILINE)}
    running, at_exit = {}, {}
    for match in re.finditer(r'^D3D12 (\w+) #(\d+) \w+ frame=(\d+): ', text, re.MULTILINE):
        messages = at_exit if exit_report and match.start() > exit_report.start() else running
        message = messages.setdefault(int(match[2]), {'severity': match[1], 'count': 0, 'at_least': False, 'first_frame': int(match[3])})
        message['count'] += 1
    for id in logged_times.keys() & running.keys():
        # total 줄이 없으면 app 이 Graphics::Shutdown 전에 끝난 것이다. 제한을 알리는 줄은 한 번 더 났을 때 쓰므로 그보다 하나는 많다.
        running[id]['count'] = totals.get(id, logged_times[id] + 1)
        running[id]['at_least'] = id not in totals
    return {
        'status': status.group(1) if status else None,
        'logged': status.group(1).startswith('logged') if status else None,
        'running': running,
        'at_exit': at_exit if exit_report else None,
    }


def summarize_log(log):
    text = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
    reasons = re.findall(r'^exit reason=(.*)$', text, re.MULTILINE)
    smoke_test = re.search(r'RayQuery smoke test: (\w+)', text)
    return {
        'exit_reason': reasons[-1] if reasons else None,
        'smoke_test': smoke_test.group(1) if smoke_test else None,
        'd3d12': summarize_d3d12_messages(text),
        'text': text,
    }


D3D12_SEVERITIES = ['CORRUPTION', 'ERROR', 'WARNING', 'INFO', 'MESSAGE']
# 종료 report 를 device 를 놓기 전에 하므로 device 하나는 늘 살아 있다고 나온다.
LIVE_DEVICE_ID = 274


def count_by_severity(messages):
    counts = {severity: [0, False] for severity in D3D12_SEVERITIES}
    for message in messages.values():
        count = counts.setdefault(message['severity'], [0, False])
        count[0] += message['count']
        count[1] |= message['at_least']
    return counts


def sorted_by_severity(messages):
    rank = lambda severity: D3D12_SEVERITIES.index(severity) if severity in D3D12_SEVERITIES else len(D3D12_SEVERITIES)
    return sorted(messages.items(), key=lambda item: (rank(item[1]['severity']), item[1]['first_frame']))


def describe_counts(messages, always=()):
    counts = count_by_severity(messages)
    shown = [severity for severity, (count, _) in counts.items() if count or severity in always]
    return ', '.join(f'{counts[severity][0]}{"+" if counts[severity][1] else ""} {severity.lower()}' for severity in shown) or 'none'


def describe_d3d12_status(d3d12):
    return f'd3d12 messages: {d3d12["status"] or "no status line in the log"}'


def describe_d3d12(d3d12):
    """stop 이 찍는 D3D12 message 요약 줄."""
    if not d3d12['logged']:
        return [describe_d3d12_status(d3d12)]
    names = d3d12_message_names()
    at_exit = 'not reported' if d3d12['at_exit'] is None else describe_counts(d3d12['at_exit'])
    lines = [f'd3d12 while running: {describe_counts(d3d12["running"], always=("CORRUPTION", "ERROR", "WARNING"))}; at exit: {at_exit}']
    for id, message in sorted_by_severity(d3d12['running']):
        lines.append(f'  {message["severity"]} #{id} {names.get(id, "unknown")} x{message["count"]}{"+" if message["at_least"] else ""}, '
                     f'first at frame {message["first_frame"]}')
    for id, message in sorted_by_severity(d3d12['at_exit'] or {}):
        lines.append(f'  at exit: {message["severity"]} #{id} {names.get(id, "unknown")} x{message["count"]}')
    return lines


def describe_exit(summary, exit_code=None):
    if summary['exit_reason']:
        return f'exit reason={summary["exit_reason"]}'
    # 이유 줄 없이 끝났으면 (MiniEngine/Core/Automation/UnattendedRun.h) exit code 가 남은 단서다.
    if exit_code is not None:
        return f'no exit reason in the log, exit code 0x{exit_code:08X}'
    return 'no exit reason in the log, exit code unknown'


def describe_ended(instance, exit_code=None):
    return f'the app exited ({describe_exit(summarize_log(Path(instance["log"])), exit_code)}). Log: {instance["log"]}'


def log_tail(log, lines=20):
    text = summarize_log(log)['text']
    return '\n'.join(text.splitlines()[-lines:])


def stop_instance(instance, kill_now=False):
    """instance 를 끝내고 어떻게 끝났는지를 돌려준다."""
    if not is_running(instance):
        result = 'exited'
    elif kill_now:
        kill(instance['pid'])
        result = 'killed'
    else:
        session = None
        try:
            session = Session(instance)
            session.ask_ok('quit', EXIT_TIMEOUT)
            if wait_for_exit(instance['pid'], EXIT_TIMEOUT):
                result = 'quit'
            else:
                kill(instance['pid'])
                result = f'killed: did not exit in {EXIT_TIMEOUT} s after quit'
        except AppHung as hang:
            kill(instance['pid'])
            result = f'killed: hang ({hang})'
        except AppExited:
            result = 'exited'
        except HarnessError as error:
            kill(instance['pid'])
            result = f'killed: {error}'
        finally:
            if session:
                session.close()

    if INSTANCE_FILE.exists() and load_instance()['pid'] == instance['pid']:
        INSTANCE_FILE.unlink()
    summary = summarize_log(Path(instance['log']))
    return {'result': result, 'exit_reason': summary['exit_reason'], 'smoke_test': summary['smoke_test'], 'd3d12': summary['d3d12'],
            'log': instance['log']}


def stop_running_instance():
    instance = load_instance()
    if instance is None:
        return None
    return stop_instance(instance)


def launch(command, cwd):
    # app 은 viewer.py 가 끝난 뒤에도 돌아야 하므로 부른 쪽의 출력 pipe 를 물려받지 않고 job 에서도 빠진다.
    flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    options = dict(cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, close_fds=True)
    try:
        return subprocess.Popen(command, creationflags=flags | subprocess.CREATE_BREAKAWAY_FROM_JOB, **options)
    except OSError:
        print('warning: the job of this shell does not allow breakaway, so the app may exit when that job closes', file=sys.stderr)
        return subprocess.Popen(command, creationflags=flags, **options)


def prune_runs():
    """가장 최근의 KEPT_RUNS 개와 실행 중인 instance 의 run 만 남긴다."""
    instance = load_instance()
    running = Path(instance['run_dir']) if instance is not None and is_running(instance) else None
    # run 이름은 시작한 시각이라 이름 순서가 시간 순서다.
    runs = sorted((path for path in RUNS_DIR.iterdir() if path.is_dir()), key=lambda path: path.name)
    for run in runs[:-KEPT_RUNS]:
        if run != running:
            shutil.rmtree(run, ignore_errors=True)


def default_scene():
    """-scene 없이 띄운 app 이 여는 scene file 의 MiniEngine/ModelViewer 기준 경로"""
    match = re.search(r'^const wchar_t kDefaultScene\[\] = L"([^"]+)";', MODEL_VIEWER_SOURCE.read_text(encoding='utf-8'), re.M)
    if match is None:
        raise HarnessError(f'{MODEL_VIEWER_SOURCE} has no line starting with '
                           'const wchar_t kDefaultScene[] = L"<scene file path relative to MiniEngine/ModelViewer>";')
    return match.group(1)


def read_scene_file(scene):
    """scene file (MiniEngine/ModelViewer 기준 경로) 의 section. 형식은 ModelViewer.cpp 의 SceneFile 주석에 있다."""
    import configparser
    ini = configparser.ConfigParser(delimiters=('=',), comment_prefixes=(';',), interpolation=None)
    ini.optionxform = str
    if not ini.read(VIEWER_DIR / scene, encoding='utf-8'):
        raise HarnessError(f'cannot read the scene file {VIEWER_DIR / scene}')
    return ini


def scene_gltf(scene):
    """scene file 이 여는 glTF 의 MiniEngine/ModelViewer 기준 경로. app 처럼 앞에 붙은 ../ 마다 scene file 폴더를 한 단계 올라간다."""
    folder, path = posixpath.dirname(scene), read_scene_file(scene)['Model']['glTF']
    while path.startswith('../') and folder and posixpath.basename(folder) != '..':
        folder, path = posixpath.dirname(folder), path[3:]
    return posixpath.join(folder, path) if folder else path


def write_scene_file(gltf):
    """확인 script 가 만든 test scene 의 glTF 옆에 scene file 을 쓰고 그 MiniEngine/ModelViewer 기준 경로를 돌려준다.
    model 은 bounding sphere 반지름이 10 이 되게 늘리고, camera 는 그 sphere 를 화면에 담는다 (ModelViewer.cpp 의 SceneFile)."""
    gltf = Path(gltf)
    scene = gltf.with_suffix('.ini')
    scene.write_text(f'[Model]\nglTF={gltf.name}\nRadius=10\n\n[Camera]\nFit=Model\n', encoding='utf-8')
    return scene.relative_to(VIEWER_DIR).as_posix()


def start_instance(config, scene=None, build_first=True, gpu_validation=False, baseline=BASELINE_DIR):
    """scene (MiniEngine/ModelViewer 기준 scene file 경로) 을 -scene 으로 연 app 을 띄운다. None 이면 -scene 없이 default_scene() 을 연다.
    baseline 은 run 폴더에 복사해 app 이 처음 읽게 할 editor.ini 와 imgui.ini 의 폴더다."""
    opened = scene or default_scene()
    if not (VIEWER_DIR / opened).exists():
        raise HarnessError(f'missing the scene file {VIEWER_DIR / opened}')
    gltf = VIEWER_DIR / scene_gltf(opened)
    if not gltf.exists():
        hint = ' Run .\\scripts\\fetch-assets.ps1.' if (VIEWER_DIR / opened).resolve().is_relative_to(SCENES_DIR) else ''
        raise HarnessError(f'missing {gltf}, which {opened} opens.{hint}')

    stop_running_instance()
    if build_first:
        result = build(config)
        if not result['ok']:
            raise HarnessError('\n'.join(result['errors'] + [f'build failed. Log: {result["log"]}']))

    exe = exe_path(config)
    if not exe.exists():
        raise HarnessError(f'{exe} does not exist. Run build --config {config}.')

    run_id = time.strftime('%Y%m%d-%H%M%S')
    run_dir = RUNS_DIR / run_id
    suffix = 1
    while run_dir.exists():
        suffix += 1
        run_dir = RUNS_DIR / f'{run_id}-{suffix}'
    run_dir.mkdir(parents=True)
    for settings in Path(baseline).glob('*.ini'):
        shutil.copy2(settings, run_dir / settings.name)
    prune_runs()

    log = run_dir / 'app.log'
    pipe = f'ModelViewerHarness-{run_dir.name}'
    command = [str(exe), '-settings_dir', str(run_dir), '-log', str(log), '-unattended', '1', '-control', pipe, '-hdr_output', '0']
    if scene is not None:
        command += ['-scene', scene]
    if gpu_validation:
        command += ['-gpu_debug', '1']
    process = launch(command, VIEWER_DIR)

    instance = {'pid': process.pid, 'exe': str(exe), 'config': config, 'scene': opened, 'pipe': pipe,
                'run_dir': str(run_dir), 'log': str(log)}
    HARNESS_DIR.mkdir(parents=True, exist_ok=True)
    INSTANCE_FILE.write_text(json.dumps(instance, indent=2), encoding='utf-8')

    try:
        channel = Channel(pipe, STARTUP_TIMEOUT, lambda: is_running(instance))
        try:
            deadline = time.monotonic() + STARTUP_TIMEOUT
            while True:
                channel.send('ping')
                response = channel.receive(10)
                if response and response['frame'] >= 1:
                    break
                if time.monotonic() > deadline:
                    raise HarnessError(f'no frame in {STARTUP_TIMEOUT} s')
                time.sleep(0.1)
        finally:
            channel.close()
    except HarnessError as error:
        if not is_running(instance):
            exit_code = process.poll()
            raise HarnessError(f'{describe_ended(instance, None if exit_code is None else exit_code & 0xFFFFFFFF)} '
                               f'while starting.\n{log_tail(log)}')
        stop_instance(instance, kill_now=True)
        raise HarnessError(f'{error}\n{log_tail(log)}')
    return instance


# --- build --------------------------------------------------------------------------------

def find_msbuild():
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio' / 'Installer' / 'vswhere.exe'
    found = subprocess.run([str(vswhere), '-latest', '-requires', 'Microsoft.Component.MSBuild',
                            '-find', r'MSBuild\**\Bin\amd64\MSBuild.exe'], capture_output=True, text=True)
    paths = found.stdout.splitlines()
    if not paths:
        raise HarnessError('amd64 MSBuild.exe not found by vswhere')
    return paths[0]


def build(config):
    # 실행 중인 harness instance 가 exe 를 잡고 있으면 link 가 LNK1168 로 실패한다.
    stop_running_instance()
    HARNESS_DIR.mkdir(parents=True, exist_ok=True)
    log = HARNESS_DIR / f'build-{config}.log'
    # 32-bit MSBuild.exe 는 x86 host compiler 를 쓰고, 그 compiler 가 만든 pch 는 VS IDE 의 x64 host compiler 와 호환되지 않아
    # 그 뒤의 IDE build 가 C1853 으로 실패한다. 그래서 amd64 MSBuild 에 x64 host compiler 를 고른다.
    command = [find_msbuild(), str(VIEWER_DIR / 'ModelViewer.vcxproj'), f'-p:Configuration={config}', '-p:Platform=x64',
               '-p:PreferredToolArchitecture=x64', '-m', '-nologo', '-v:minimal', f'-flp:logfile={log};verbosity=normal']
    started = time.monotonic()
    completed = subprocess.run(command, capture_output=True, text=True, errors='replace')
    errors = sorted({openable_error(line.strip()) for line in completed.stdout.splitlines() if re.search(r': (fatal )?error ', line)})
    return {'ok': completed.returncode == 0, 'seconds': time.monotonic() - started, 'errors': errors, 'log': str(log)}


def openable_error(line):
    """어느 project 의 error 인지 몰라도 source 를 열 수 있게, source 의 상대 경로를 절대 경로로 바꾼 error 줄."""
    match = MSBUILD_ERROR_WITH_SOURCE.match(line)
    if match is None or Path(match['source']).is_absolute():
        return line
    return f'{Path(match["project"]).parent / match["source"]}{match["rest"]}'


def newest_source_mtime():
    sources = [path for directory in SOURCE_DIRS for path in directory.rglob('*') if path.suffix in SOURCE_SUFFIXES]
    sources += [MODEL_VIEWER_SOURCE]
    return max(path.stat().st_mtime for path in sources)


# --- 이미지 -------------------------------------------------------------------------------

def tone_map_for_view(hdr, exposure):
    import numpy as np
    rgb = np.nan_to_num(hdr[..., :3], nan=0.0, posinf=1e4) * exposure
    rgb = np.clip(rgb, 0.0, None)
    mapped = rgb / (1.0 + rgb)
    srgb = np.where(mapped <= 0.0031308, mapped * 12.92, 1.055 * np.power(mapped, 1.0 / 2.4) - 0.055)
    return (np.clip(srgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def describe_difference(first, second):
    import numpy as np
    differing = int(np.count_nonzero(np.any(first.view(np.uint32) != second.view(np.uint32), axis=2)))
    largest = float(np.nanmax(np.abs(first - second)))
    return f'{differing} pixels differ, max abs diff {largest:g}'


def make_view(original, target, view, crop, max_width, exposure):
    from PIL import Image
    if target in ('scene', 'pt'):
        image = Image.fromarray(tone_map_for_view(read_dds_rgba32f(original), exposure))
    else:
        # overlay 는 premultiplied alpha 라서 RGB 가 곧 검은 바탕에 합성한 색이다. display 에는 alpha 가 없다.
        image = Image.open(original).convert('RGB')
    if crop:
        x, y, width, height = crop
        image = image.crop((x, y, x + width, y + height))
    scale = min(1.0, max_width / image.width)
    if scale < 1.0:
        image = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    image.save(view)
    return image.size, scale


def capture_with_state(session, target, name):
    """image 의 경로, 그 frame, 그 frame 의 state 를 남긴 파일의 경로가 든 capture 의 응답."""
    return session.ask_ok(f'capture {target} {shots_dir(session.instance) / name}')


def capture(session, target, name):
    return Path(capture_with_state(session, target, name)['path'])


def next_shot_name(instance, target):
    shots = Path(instance['run_dir']) / 'shots'
    index = len(list(shots.glob('*-view.png'))) + 1 if shots.exists() else 1
    return f'{index:03d}-{target}'


# --- subcommand ---------------------------------------------------------------------------

@contextlib.contextmanager
def running_session():
    """명령을 주고받는 동안 app 이 끝나거나 멈추면 무엇이 일어났는지 알리는 HarnessError 로 바꾼다."""
    instance = require_running()
    session = Session(instance)
    try:
        yield session
    except AppExited:
        if not wait_for_exit(instance['pid'], 5):
            raise HarnessError(f'the connection to the app closed but the app is still running; stop ends it. Log: {instance["log"]}')
        raise HarnessError(describe_ended(instance, session.exit_code()))
    except AppHung as hang:
        # kill 은 stop 한 곳에서 한다. 멈춘 app 은 debugger 로 볼 수 있게 남긴다.
        raise HarnessError(f'the app is hung ({hang}). It is left running; stop kills it. Log: {instance["log"]}')
    finally:
        session.close()


def resolve_references(command, responses):
    def value_of(reference):
        name, *keys = reference[1].split('.')
        if name not in responses:
            raise HarnessError(f'{{{reference[1]}}} names no earlier response. Named so far: {", ".join(responses)}')
        value = responses[name]
        for key in keys:
            if not isinstance(value, dict) or key not in value:
                raise HarnessError(f'{{{reference[1]}}}: no {key!r} there')
            value = value[key]
        if isinstance(value, (dict, list)):
            raise HarnessError(f'{{{reference[1]}}} is not a single value')
        return str(value).lower() if isinstance(value, bool) else str(value)
    return SCENARIO_REFERENCE.sub(value_of, command)


def shots_dir(instance):
    shots = Path(instance['run_dir']) / 'shots'
    shots.mkdir(exist_ok=True)
    return shots


def run_lines(session, lines, echo):
    """명령을 차례로 보내고 (이름을 붙인 응답, 실패한 명령의 응답 또는 None) 을 돌려준다. 실패한 명령에서 멈춘다.
    줄의 형식은 run 의 --help 가 정한다."""
    responses = {'shots': shots_dir(session.instance).as_posix()}
    for line in lines:
        named = SCENARIO_NAMED_LINE.match(line)
        name, command = named.groups() if named else (None, line)
        command = resolve_references(command, responses)
        if echo:
            print(f'> {command}')
        response = session.ask(command)
        if echo:
            print(json.dumps(response, ensure_ascii=False))
        if not response['ok']:
            return responses, response
        if name:
            responses[name] = response
    return responses, None


def run_commands(lines):
    with running_session() as session:
        _, failed = run_lines(session, lines, echo=True)
    return 0 if failed is None else 1


def scenario_lines(path):
    lines = [line.strip() for line in Path(path).read_text(encoding='utf-8').splitlines()]
    return [line for line in lines if line and not line.startswith('#')]


def command_build(args):
    result = build(args.config)
    for error in result['errors']:
        print(error)
    print(f'build {args.config}: {"ok" if result["ok"] else "FAILED"} in {result["seconds"]:.0f} s. Log: {result["log"]}')
    return 0 if result['ok'] else 1


def command_start(args):
    if args.gpu_validation and args.config != 'Debug':
        raise HarnessError(f'--gpu-validation works only in Debug: start does not turn on the D3D12 debug layer in {args.config}, '
                           'so GPU-based validation would stay off')
    instance = start_instance(args.config, args.scene, not args.no_build, args.gpu_validation)
    session = Session(instance)
    try:
        status = session.ask_ok('status')
    finally:
        session.close()
    print(f'started {instance["config"]} pid {instance["pid"]}: client {status["client"]}, scene {status["scene"]}, '
          f'ui {status["ui"]}, gpu {status["gpu"]}')
    print(describe_d3d12_status(summarize_log(Path(instance['log']))['d3d12']))
    print(f'run dir: {instance["run_dir"]}')
    print(f'log: {instance["log"]}')
    return 0


def command_do(args):
    return run_commands(args.commands)


def command_run(args):
    return run_commands(scenario_lines(args.file))


def parse_crop(text):
    values = [int(value) for value in text.split(',')]
    if len(values) != 4:
        raise argparse.ArgumentTypeError('expected X,Y,W,H')
    return values


def command_shot(args):
    with running_session() as session:
        name = next_shot_name(session.instance, args.target)
        response = capture_with_state(session, args.target, name)
    original = Path(response['path'])
    view = original.with_name(f'{name}-view.png')
    size, scale = make_view(original, args.target, view, args.crop, args.max_width, args.exposure)
    print(f'original: {original}')
    print(f'state: {response["state"]} (frame {response["frame"]})')
    print(f'view: {view} ({size[0]}x{size[1]}, view pixel = original pixel x {scale:.4g}'
          + (f' after crop at {args.crop[0]},{args.crop[1]})' if args.crop else ')'))
    return 0


def command_stop(args):
    instance = load_instance()
    if instance is None:
        print('no harness instance')
        return 0
    result = stop_instance(instance, args.kill)
    print(f'{result["result"]}; exit reason={result["exit_reason"]}; smoke test={result["smoke_test"]}')
    for line in describe_d3d12(result['d3d12']):
        print(line)
    print(f'log: {result["log"]}')
    return 0 if result['exit_reason'] == 'normal' else 1


# --- snapshot -----------------------------------------------------------------------------

SNAPSHOT_DIR = HARNESS_DIR / 'kept' / 'snapshots'
# -scene 없이 연 scene 의 처음 시점 (start) 과, 그 scene 에서 해가 닿는 곳과 닿지 않는 곳이 고루 보이도록 고른 시점
SNAPSHOT_POSES = {
    'start': None,
    'courtyard-up': '0 300 0 1.5708 1.2',
    'shadowed-wall': '1500 150 0 1.5708 0.05',
    'gallery': '-1200 150 400 -1.5708 0.1',
}
# harness baseline 은 결정성을 위해 이 app 의 기본 효과를 끈다 (scripts/harness-baseline/editor.ini). app-default 는 사용자가 보는 화면이다.
SNAPSHOT_SETTINGS = {
    'baseline': {},
    'app-default': {'Graphics/AA/TAA/Enable': 'on', 'Graphics/Motion Blur/Enable': 'on', 'Graphics/HDR/Adaptive Exposure': 'on'},
}
# adaptive exposure 가 수렴하고 TAA 의 history 가 차도록 시점마다 기다리는 frame 수. Adaptation Rate 0.05 에서 충분하다고 본다 (추측).
SNAPSHOT_SETTLE_FRAMES = {'baseline': SETTLE_FRAMES, 'app-default': 300}
# scene 에서 이만큼 넘게 달라진 pixel 을 차이 mask 에 흰색으로 칠한다. TAA 의 jitter 같은 작은 차이와 구별하려는 것이다.
SNAPSHOT_LARGE_CHANGE = 0.25
SNAPSHOT_PANEL_SIZE = (640, 360)
# compare 가 CHANGED 로 표시하는 기준. snapshot save 가 같은 code 를 두 번 띄워 잰 차이의 몇 배를 넘고, scene 의 일정 비율도 넘어야 한다.
# 같은 code 의 실행 사이에서 크게 바뀐 pixel 은 scene 의 0.12% 까지였고, 한 시점에서도 실행에 따라 0.00% ~ 0.04% 로 흔들렸다.
# SSAO 와 함께 linear depth 가 빠진 bug 는 0.30% ~ 6.74% 였다.
SNAPSHOT_SAME_CODE_FACTOR = 4
SNAPSHOT_SAME_CODE_FLOOR = 0.001


def default_scene_pose():
    """harness 가 -scene 없이 띄운 app 의 처음 시점 (default_scene() 의 [Camera]) 을 pose 명령의 인자로."""
    camera = read_scene_file(default_scene())['Camera']
    return ' '.join(camera['Position'].split(',') + [camera['Heading'], camera['Pitch']])


def code_revision():
    """HEAD 의 짧은 hash. commit 하지 않은 변경이 있으면 + 를 붙인다."""
    def git(*arguments):
        return subprocess.run(['git', *arguments], cwd=REPO_ROOT, capture_output=True, text=True, check=True).stdout.strip()
    return git('rev-parse', '--short', 'HEAD') + ('+' if git('status', '--porcelain') else '')


def capture_snapshot(config, build_first, folder):
    """SNAPSHOT_SETTINGS x SNAPSHOT_POSES 의 scene 과 display 를 folder 에 찍고, 찍은 shot 과 app 의 끝을 돌려준다."""
    folder.mkdir(parents=True)
    instance = start_instance(config, build_first=build_first)
    shots = []
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            for settings_name, settings in SNAPSHOT_SETTINGS.items():
                if settings:
                    session.ask_ok('settings set Engine Tuning: ' + '; '.join(f'{key}={value}' for key, value in settings.items()))
                for pose_name, pose in SNAPSHOT_POSES.items():
                    session.ask_ok(f'pose {pose or default_scene_pose()}')
                    session.ask_ok(f'frames {SNAPSHOT_SETTLE_FRAMES[settings_name]}')
                    name = f'{settings_name}-{pose_name}'
                    for target in ['scene', 'display']:
                        session.ask_ok(f'capture {target} {folder / f"{name}-{target}"}')
                    shots.append(name)
        finally:
            session.close()
    except HarnessError:
        stop_instance(instance, kill_now=True)
        raise
    return shots, stop_instance(instance)


def snapshot_ended_well(stopped):
    running = [message for message in stopped['d3d12']['running'].values() if message['severity'] in ('CORRUPTION', 'ERROR', 'WARNING')]
    return stopped['exit_reason'] == 'normal' and stopped['d3d12']['logged'] and not running


def describe_snapshot_end(stopped):
    return [f'app: {stopped["result"]}; exit reason={stopped["exit_reason"]}'] + describe_d3d12(stopped['d3d12'])


def shot_change(before_folder, after_folder, name):
    """한 shot 에서 scene 이 바뀐 pixel 과 크게 바뀐 pixel 의 mask, after 의 NaN 과 Inf 수, display 가 바뀐 pixel 수."""
    import numpy as np
    from PIL import Image
    before = read_dds_rgba32f(before_folder / f'{name}-scene.dds')[..., :3]
    after = read_dds_rgba32f(after_folder / f'{name}-scene.dds')[..., :3]
    if before.shape != after.shape:
        raise HarnessError(f'{name}: scene is {before.shape} before and {after.shape} after')
    changed = np.any(before.view(np.uint32) != after.view(np.uint32), axis=2)
    relative = np.abs(np.nan_to_num(after - before)).max(axis=2) / np.maximum(np.abs(before).max(axis=2), 1e-3)
    displays = [np.asarray(Image.open(folder / f'{name}-display.png').convert('RGB')) for folder in (before_folder, after_folder)]
    return {'changed': changed, 'large': changed & (relative > SNAPSHOT_LARGE_CHANGE),
            'nonfinite': int(np.count_nonzero(~np.isfinite(after).all(axis=2))),
            'display_changed': int(np.count_nonzero(np.any(displays[0] != displays[1], axis=2)))}


def change_counts(change):
    return {'changed': int(change['changed'].sum()), 'large': int(change['large'].sum()), 'pixels': int(change['changed'].size)}


def exceeds_same_code(counts, same_code):
    """같은 code 로 찍어도 생기는 차이보다 크게 바뀌었는가. 같은 code 에서 바뀌지 않은 shot 은 한 pixel 이라도 바뀌면 크다."""
    if same_code['changed'] == 0:
        return counts['changed'] > 0
    return counts['large'] / counts['pixels'] > max(SNAPSHOT_SAME_CODE_FACTOR * same_code['large'] / same_code['pixels'],
                                                    SNAPSHOT_SAME_CODE_FLOOR)


def change_sheet(before_folder, after_folder, name, change):
    """before display, after display, scene 의 차이 mask 를 나란히 놓은 image."""
    import numpy as np
    from PIL import Image
    mask = np.zeros(change['changed'].shape, dtype=np.uint8)
    mask[change['changed']] = 96
    mask[change['large']] = 255
    width, height = SNAPSHOT_PANEL_SIZE
    sheet = Image.new('RGB', (width * 3 + 20, height), (255, 0, 255))
    panels = [Image.open(before_folder / f'{name}-display.png'), Image.open(after_folder / f'{name}-display.png'), Image.fromarray(mask)]
    for index, panel in enumerate(panels):
        sheet.paste(panel.convert('RGB').resize(SNAPSHOT_PANEL_SIZE, Image.LANCZOS), (index * (width + 10), 0))
    return sheet


def command_snapshot_save(args):
    folder = SNAPSHOT_DIR / args.name
    if folder.exists():
        if not args.replace:
            raise HarnessError(f'{folder} exists. Use --replace to take it again')
        shutil.rmtree(folder)
    shots, stopped = capture_snapshot(args.config, not args.no_build, folder)
    ended_well = snapshot_ended_well(stopped)
    for line in describe_snapshot_end(stopped):
        print(line)
    # TAA 와 adaptive exposure 는 같은 code 에서도 실행마다 결과를 바꾸므로, 한 번 더 띄워 그 차이를 compare 의 기준으로 남긴다.
    same_code_folder = folder / 'same-code'
    _, stopped = capture_snapshot(args.config, False, same_code_folder)
    ended_well &= snapshot_ended_well(stopped)
    for line in describe_snapshot_end(stopped):
        print(line)
    same_code = {name: change_counts(shot_change(folder, same_code_folder, name)) for name in shots}
    manifest = {'code': code_revision(), 'config': args.config, 'created': time.strftime('%Y-%m-%d %H:%M:%S'), 'shots': shots,
                'poses': {name: pose or default_scene_pose() for name, pose in SNAPSHOT_POSES.items()}, 'settings': SNAPSHOT_SETTINGS,
                'same_code': same_code}
    (folder / 'snapshot.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    for name in shots:
        counts = same_code[name]
        print(f'same code {name}: {counts["changed"]} pixels changed, {counts["large"]} by more than {SNAPSHOT_LARGE_CHANGE:.0%}')
    print(f'saved {len(shots)} shots of {manifest["code"]} to {folder}')
    return 0 if ended_well else 1


def command_snapshot_compare(args):
    before_folder = SNAPSHOT_DIR / args.name
    manifest_path = before_folder / 'snapshot.json'
    if not manifest_path.exists():
        raise HarnessError(f'no snapshot {before_folder}. Take it with snapshot save {args.name} before changing the code')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    poses = {name: pose or default_scene_pose() for name, pose in SNAPSHOT_POSES.items()}
    if manifest['poses'] != poses or manifest['settings'] != SNAPSHOT_SETTINGS or manifest['config'] != args.config:
        raise HarnessError(f'{args.name} was taken with other poses, settings or config. Take a new snapshot with this viewer.py')

    after_folder = before_folder / f'compare-{time.strftime("%Y%m%d-%H%M%S")}'
    shots, stopped = capture_snapshot(args.config, not args.no_build, after_folder)
    lines = [f'before: {manifest["code"]} ({manifest["created"]}), after: {code_revision()}',
             f'CHANGED marks a shot that changed more than two runs of the before code did (more than {SNAPSHOT_SAME_CODE_FACTOR}x '
             f'the pixels changed by more than {SNAPSHOT_LARGE_CHANGE:.0%}, and more than {SNAPSHOT_SAME_CODE_FLOOR:.2%} of the scene)']
    nonfinite = 0
    for name in shots:
        change = shot_change(before_folder, after_folder, name)
        change_sheet(before_folder, after_folder, name, change).save(after_folder / f'{name}-sheet.png')
        counts, same_code = change_counts(change), manifest['same_code'][name]
        nonfinite += change['nonfinite']
        lines.append(f'{"CHANGED" if exceeds_same_code(counts, same_code) else "same   "} {name}: scene {counts["changed"]} pixels changed '
                     f'({counts["changed"] / counts["pixels"]:.2%}), {counts["large"]} by more than {SNAPSHOT_LARGE_CHANGE:.0%} '
                     f'({counts["large"] / counts["pixels"]:.2%}; same code {same_code["large"] / same_code["pixels"]:.2%}), '
                     f'{change["nonfinite"]} NaN or Inf after; display {change["display_changed"]} pixels changed')
    lines += describe_snapshot_end(stopped)
    lines.append(f'sheets (before display, after display, scene change: gray changed, white by more than {SNAPSHOT_LARGE_CHANGE:.0%}): '
                 f'{after_folder}')
    (after_folder / 'report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    for line in lines:
        print(line)
    return 0 if snapshot_ended_well(stopped) and nonfinite == 0 else 1


def hash_user_settings():
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None for path in USER_SETTINGS}


def command_selftest(args):
    failures = []

    def check(name, passed, detail=''):
        print(f'{"PASS" if passed else "FAIL"}  {name}' + (f': {detail}' if detail != '' else ''), flush=True)
        if not passed:
            failures.append(name)
        return passed

    user_settings = hash_user_settings()
    try:
        selftest_running_app(args, check)
    except (HarnessError, ImageFileError) as error:
        check('selftest ran to the end', False, str(error))
    finally:
        instance = load_instance()
        if instance is not None and is_running(instance):
            stop_instance(instance, kill_now=True)
        check('user editor.ini and imgui.ini are unchanged', hash_user_settings() == user_settings)

    print(f'{len(failures)} failed' if failures else 'all passed')
    return 1 if failures else 0


def without_time_varying(state):
    kept = copy.deepcopy(state)
    for path in TIME_VARYING_STATE:
        parent = kept
        for key in path[:-1]:
            parent = parent.get(key, {})
        parent.pop(path[-1], None)
    return kept


def part_of_state(state, path):
    """state <path> 가 돌려줄, path 가 가리키는 member 만 남긴 state."""
    keys = path.split('.')
    part = inner = {}
    for key in keys[:-1]:
        state = state[key]
        inner[key] = {}
        inner = inner[key]
    inner[keys[-1]] = state[keys[-1]]
    return part


def merge_state(state, part):
    """part 를 state 에 합친다. settings.Engine Tuning 과 settings.Path Tracer 처럼 바깥 key 가 같은 object 는 그 안에서 합친다."""
    for key, value in part.items():
        if isinstance(value, dict) and isinstance(state.get(key), dict):
            merge_state(state[key], value)
        else:
            state[key] = value
    return state


def read_state(session, paths):
    """paths 가 가리키는 부분을 한 state 로 합친다."""
    state = {}
    for path in paths:
        merge_state(state, session.ask_ok(f'state {path}')['state'])
    return state


def record_change(session, changes, name, line, *paths):
    """line 을 보내고, 그 명령이 바꾼 state 의 paths 를 check_changes 가 볼 수 있게 changes 에 더한다."""
    before = read_state(session, paths)
    reply = session.ask_ok(line)
    changes.append((name, reply.get('state'), read_state(session, paths), before))
    return reply


def check_changes(check, label, changes):
    """명령마다 응답의 state 가 바로 뒤에 읽은 state 와 같고, 명령 전과 달라야 한다. 시간에 따라 바뀌는 값은 비교에서 뺀다."""
    wrong = [name for name, replied, read, before in changes
             if replied is None or without_time_varying(replied) != without_time_varying(read)
             or without_time_varying(read) == without_time_varying(before)]
    check(label, not wrong, ', '.join(wrong) or ', '.join(name for name, *_ in changes))


def check_state(session, check):
    """state 가 판정의 SSOT 로서, 같은 값을 주는 다른 명령과 상태를 바꾼 명령의 응답과 shot 의 state 파일에 맞는지 본다.
    UI 를 숨기고 TAA 를 끈 상태에서 부르고, 그 상태로 되돌려 놓는다."""
    import numpy as np

    usage = session.ask_ok('help')['commands']['state']
    check('state usage gives the units and axes', all(term in usage for term in ['world units', 'radians', '+y', 'client pixels']), usage)

    before_reading = read_dds_rgba32f(capture(session, 'scene', 'selftest-state-before-reading'))
    whole = session.ask_ok('state')['state']
    session.ask_ok('frames 60')
    later = session.ask_ok('state')['state']
    check('state stays the same for 60 frames but the frame and PT progress',
          without_time_varying(later) == without_time_varying(whole) and later['frame'] >= whole['frame'] + 60,
          f'frame {whole["frame"]} -> {later["frame"]}')

    paths = [key for key in whole if key != 'frame'] + ['settings.Engine Tuning', 'camera.forward.x']
    differing = [path for path in paths
                 if without_time_varying(session.ask_ok(f'state {path}')['state']) != without_time_varying(part_of_state(whole, path))]
    check('state <path> is that part of the whole state', not differing, ', '.join(differing) or ', '.join(paths))

    wrong = session.ask('state no.such.path')
    unchanged = without_time_varying(session.ask_ok('state')['state']) == without_time_varying(whole)
    check('state with an unknown path fails, names the keys there and changes nothing',
          not wrong['ok'] and 'camera' in wrong.get('error', '') and unchanged, wrong.get('error'))

    after_reading = read_dds_rgba32f(capture(session, 'scene', 'selftest-state-after-reading'))
    check('scene is bit-identical before and after reading state', np.array_equal(before_reading.view(np.uint32), after_reading.view(np.uint32)),
          describe_difference(before_reading, after_reading))

    scene = session.instance['scene']
    check('state scene is the scene file start opened and state model the glTF it opens',
          whole['scene']['path'] == scene and whole['model']['path'] == scene_gltf(scene), f'{whole["scene"]}, {whole["model"]["path"]}')

    status = session.ask_ok('status')
    ui, engine = whole['ui'], whole['engine']
    as_status = {
        'ui': 'shown' if ui['visible'] else 'hidden',
        'client': f'{ui["client"]["width"]}x{ui["client"]["height"]}',
        'scene': f'{ui["scene"]["width"]}x{ui["scene"]["height"]}',
        'app': {'camera': whole['camera']['controller']},
        **{key: engine[key] for key in ['config', 'gpu', 'exe', 'hdr_output', 'hdr_display', 'd3d12_messages']},
    }
    differing = [f'{key} {status[key]} != {value}' for key, value in as_status.items() if status[key] != value]
    check('state ui, engine and camera controller are what status reports', not differing, '; '.join(differing))

    sections = session.ask('settings get')['error'].split('Sections: ')[1].split(', ')
    differing = [section for section in sections if whole['settings'].get(section) != session.ask_ok(f'settings get {section}')['values']]
    check('state settings has every section as settings get reads it', not differing and sorted(sections) == sorted(whole['settings']),
          ', '.join(differing) or ', '.join(sections))

    progress = {key: value for key, value in session.ask_ok('pt status').items() if key != 'ok'}
    check('state pt is what pt status reports but the progress', without_time_varying({'pt': progress}) == without_time_varying({'pt': whole['pt']}),
          f'{progress} vs {whole["pt"]}')

    changes = []
    change = functools.partial(record_change, session, changes)

    pose_keys = ['x', 'y', 'z', 'heading', 'pitch']
    start = session.ask_ok('pose')
    moved = change('pose', 'pose ' + ' '.join(repr(float(start[key]) + offset) for key, offset in zip(pose_keys, [12.5, -3.25, 7.75, 0.125, 0.0625])),
                   'camera')
    camera = session.ask_ok('state camera')['state']['camera']
    check('state camera is the pose and follows it', all(camera[key] == moved[key] for key in pose_keys) and camera != whole['camera'],
          ' '.join(str(camera[key]) for key in pose_keys))
    described = dict(item.split('=', 1) for item in session.ask_ok('describe')['line'].split('; '))
    check('state camera is the camera describe writes',
          [str(camera[key]) for key in pose_keys] == described['camera'].split(',') + [described['heading'], described['pitch']],
          f'camera={described["camera"]}; heading={described["heading"]}; pitch={described["pitch"]}')
    section = session.ask_ok('settings get Editor Camera')['values']
    check('state camera agrees with the Editor Camera section, which has only the move speed',
          section == {'Move Speed': str(camera['move_speed'])}, section)

    shot = capture_with_state(session, 'display', 'selftest-state-display')
    saved = json.loads(Path(shot['state']).read_text(encoding='utf-8'), parse_float=str)
    now = session.ask_ok('state')['state']
    check('shot leaves the state of its frame next to the image',
          saved['frame'] == shot['frame'] and without_time_varying(saved) == without_time_varying(now) and saved['camera'] == camera,
          f'frame {saved["frame"]} in the file, {shot["frame"]} in the reply')

    exposure = 'Graphics/HDR/Exposure'
    original_exposure = whole['settings']['Engine Tuning'][exposure]
    change('settings set', f'settings set Engine Tuning: {exposure}=2', 'settings.Engine Tuning')
    session.ask_ok(f'settings set Engine Tuning: {exposure}={original_exposure}')
    change('ui show', 'ui show', 'ui')
    change('panel open', 'panel Engine Tuning open', 'ui')
    change('panel close', 'panel Engine Tuning close', 'ui')
    change('ui hide', 'ui hide', 'ui')

    original_view = whole['settings']['Ray Tracing View']['View']
    session.ask_ok('settings set Ray Tracing View: View=Validation: Depth Comparison')
    session.ask_ok('frames 2')
    captured = change('validation capture', 'validation capture', 'validation')
    validation = session.ask_ok('state validation')['state']['validation']
    check('state validation has the values validation capture replies',
          validation['captured'] and validation.get('values') == captured['values'] and not whole['validation']['captured'],
          captured['line'])
    session.ask_ok(f'settings set Ray Tracing View: View={original_view}')
    session.ask_ok(f'frames {VALIDATION_VIEW_SETTLE_FRAMES}')

    check_changes(check, 'commands that change the state reply with the changed part as state then reads it', changes)

    session.ask_ok('pose ' + ' '.join(str(start[key]) for key in pose_keys))
    session.ask_ok(f'frames {SETTLE_FRAMES}')


LIGHT_OBJECT = 'Directional Light'
LIGHT_SECTION = 'Directional Light'


def numbers_of(text):
    import numpy as np
    return np.array([float(value) for value in text.split(',')])


def degrees_between(first, second):
    difference = (first - second) % 360.0
    return min(difference, 360.0 - difference)


def elevation_and_azimuth_of(direction):
    import numpy as np
    return np.degrees(np.arcsin(direction[1])), np.degrees(np.arctan2(direction[2], direction[0])) % 360.0


def rotate(quaternion, vector):
    """x, y, z, w 순서의 단위 quaternion 으로 vector 를 돌린다."""
    import numpy as np
    axis, w = np.asarray(quaternion[:3], dtype=float), float(quaternion[3])
    vector = np.asarray(vector, dtype=float)
    twice = 2.0 * np.cross(axis, vector)
    return vector + w * twice + np.cross(axis, twice)


def quaternion_of(fields):
    import numpy as np
    return np.array([float(fields[axis]) for axis in 'xyzw'])


def check_light(session, check, changes):
    """light set 이 gizmo 와 panel 이 바꾸는 값을 바꾸고, state light 가 조명이 쓰는 방향과 editor.ini 에 남는 값에 맞는지 본다.
    UI 를 숨기고 TAA 를 끈 상태에서 부르고, light 를 처음 상태로 되돌려 놓는다."""
    import numpy as np

    def light_state():
        return session.ask_ok('state light')['state']['light']

    def saved():
        return session.ask_ok(f'settings get {LIGHT_SECTION}')['values']

    def restore_saved(values):
        session.ask_ok(f'settings set {LIGHT_SECTION}: ' + '; '.join(f'{key}={value}' for key, value in values.items()))

    usage = session.ask_ok('help')['commands']['light']
    check('light usage gives the units and axes', all(term in usage for term in ['degrees', '+x', '+z', '+y', '-Z', 'world units', 'lux', 'reset']), usage)

    original, original_saved = light_state(), saved()
    direction = vector_of(original['direction'])
    elevation, azimuth = elevation_and_azimuth_of(direction)
    check('state light direction is the +Z of its rotation and the Direction saved in editor.ini, and elevation, azimuth are of that direction',
          np.abs(rotate(quaternion_of(original['rotation']), [0, 0, 1]) - direction).max() <= 1e-6
          and np.array_equal(numbers_of(original_saved['Direction']).astype(np.float32), direction.astype(np.float32))
          and abs(float(original['elevation']) - elevation) <= 1e-4 and degrees_between(float(original['azimuth']), azimuth) <= 1e-4,
          f'{original["rotation"]}, {original["direction"]}, saved {original_saved["Direction"]}; '
          f'elevation {original["elevation"]} vs {elevation}, azimuth {original["azimuth"]} vs {azimuth}')

    session.ask_ok(f'frames {SETTLE_FRAMES}')
    reference = read_dds_rgba32f(capture(session, 'scene', 'selftest-light-before'))

    misread = []
    for elevation, azimuth in [(30, 120), (5, 350), (-20, 45)]:
        record_change(session, changes, f'light set elevation={elevation} azimuth={azimuth}', f'light set elevation={elevation} azimuth={azimuth}', 'light')
        light = light_state()
        if abs(float(light['elevation']) - elevation) > 1e-4 or degrees_between(float(light['azimuth']), azimuth) > 1e-4:
            misread.append(f'{elevation},{azimuth} read {light["elevation"]},{light["azimuth"]}')
    check('light set elevation, azimuth reads them back, below the horizon too', not misread, '; '.join(misread))

    session.ask_ok(f'frames {SETTLE_FRAMES}')
    lowered = read_dds_rgba32f(capture(session, 'scene', 'selftest-light-lowered'))
    restore_saved(original_saved)
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    returned = read_dds_rgba32f(capture(session, 'scene', 'selftest-light-returned'))
    check('scene changes with the light direction and is bit-identical once the saved values come back',
          not np.array_equal(reference.view(np.uint32), lowered.view(np.uint32)) and np.array_equal(reference.view(np.uint32), returned.view(np.uint32)),
          describe_difference(reference, returned))

    requested = np.array([0.3, 0.8, 0.5])
    record_change(session, changes, 'light set direction', 'light set direction=0.3,0.8,0.5', 'light')
    read = vector_of(light_state()['direction'])
    check('light set direction reads back as the normalized direction', np.abs(read - requested / np.linalg.norm(requested)).max() <= 1e-5, read.tolist())

    rotation = np.array([0.1, -0.7, 0.2, 0.6])
    rotation /= np.linalg.norm(rotation)
    record_change(session, changes, 'light set rotation', 'light set rotation=' + ','.join(f'{value:.9g}' for value in rotation), 'light')
    light = light_state()
    read = quaternion_of(light['rotation'])
    check('light set rotation reads back as the normalized quaternion, and the direction is its +Z',
          abs(abs(float(np.dot(read, rotation))) - 1.0) <= 1e-6 and np.abs(vector_of(light['direction']) - rotate(rotation, [0, 0, 1])).max() <= 1e-6,
          f'{light["rotation"]} vs {rotation.tolist()}, direction {light["direction"]}')

    whole = without_time_varying(session.ask_ok('state')['state'])
    accepted = []
    for request in ['elevation=-91', 'elevation=91', 'direction=0,0,0', 'rotation=0,0,0,0', 'intensity=-1', 'color=1.5,0,0', 'color=1,1',
                    'elevation=abc', 'direction=1,2', 'brightness=1', 'elevation=10 direction=1,1,1', 'rotation=0,0,0,1 direction=0,1,0',
                    'elevation=10 elevation=20', '']:
        response = session.ask(f'light set {request}')
        if response['ok'] or without_time_varying(session.ask_ok('state')['state']) != whole:
            accepted.append(request or '(nothing)')
    check('light set out of range or malformed fails and changes nothing', not accepted, ', '.join(accepted))

    below = session.ask_ok('light set direction=0,-1,0')['state']['light']
    zenith = session.ask_ok('light set elevation=90')
    check('light set takes a direction straight down, and says a vertical direction has no azimuth',
          float(below['elevation']) == -90 and float(below['azimuth']) == 0 and 'azimuth' in zenith.get('note', '')
          and float(zenith['state']['light']['elevation']) == 90,
          f'{below["elevation"]}, {below["azimuth"]}; {zenith.get("note")}')

    session.ask_ok('light set elevation=40 azimuth=200')
    described, set_light = session.ask_ok('describe')['line'], light_state()
    session.ask_ok('light set elevation=20 azimuth=10')
    session.ask_ok(f'restore {described}')
    check('restore of the describe line after light set gives that light again', light_state() == set_light, f'{set_light} -> {light_state()}')

    restore_saved(original_saved)
    position = {'x': '100.5', 'y': '200.25', 'z': '-300.75'}
    record_change(session, changes, 'light set position', 'light set position=' + ','.join(position[axis] for axis in 'xyz'), 'light')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    moved = read_dds_rgba32f(capture(session, 'scene', 'selftest-light-moved'))
    check('light set position reads back as given and leaves the scene bit-identical',
          {axis: str(value) for axis, value in light_state()['position'].items()} == position and np.array_equal(reference.view(np.uint32), moved.view(np.uint32)),
          describe_difference(reference, moved))

    record_change(session, changes, 'light set color, intensity', 'light set color=0.5,0.25,1 intensity=2.5', 'light')
    light, values = light_state(), saved()
    check('light set color and intensity read back as given and are what editor.ini keeps',
          light['color'] == {'r': '0.5', 'g': '0.25', 'b': 1} and light['intensity'] == '2.5'
          and values['Color'] == '0.5,0.25,1' and values['Intensity'] == '2.5',
          f'{light["color"]}, {light["intensity"]}; saved {values["Color"]}, {values["Intensity"]}')

    session.ask_ok('light set direction=0.3,0.8,0.5 position=1,2,3')
    record_change(session, changes, 'light reset', 'light reset', 'light')
    light = light_state()
    check('light reset sets the transform to no rotation at the origin and keeps the color and intensity',
          np.array_equal(quaternion_of(light['rotation']), [0, 0, 0, 1]) and np.array_equal(vector_of(light['direction']), [0, 0, 1])
          and np.array_equal(vector_of(light['position']), [0, 0, 0])
          and light['color'] == {'r': '0.5', 'g': '0.25', 'b': 1} and light['intensity'] == '2.5',
          str(light))

    restore_saved(original_saved)
    check('light is back as it was', light_state() == original, f'{original} -> {light_state()}')


def check_look_at(session, check, changes):
    """pose look-at 이 위치를 두고 점을 보게 하는지 본다. camera 를 처음 pose 로 되돌려 놓는다."""
    import numpy as np

    usage = session.ask_ok('help')['commands']['pose']
    check('pose usage gives look-at and the units', all(term in usage for term in ['look-at', 'world units', 'radians']), usage)

    start = session.ask_ok('state camera')['state']['camera']
    position = vector_of(start)
    wrong = []
    for name, target in [('below', position + np.array([300.0, -150.0, 40.0])), ('model center', np.zeros(3))]:
        record_change(session, changes, f'pose look-at {name}', 'pose look-at ' + ' '.join(repr(float(value)) for value in target), 'camera')
        camera = session.ask_ok('state camera')['state']['camera']
        expected = (target - position) / np.linalg.norm(target - position)
        if np.abs(vector_of(camera['forward']) - expected).max() > 1e-5 or any(camera[axis] != start[axis] for axis in 'xyz'):
            wrong.append(f'{name}: forward {camera["forward"]} vs {expected.tolist()}, position {[camera[axis] for axis in "xyz"]}')
    check('pose look-at keeps the position and points forward at the target', not wrong, '; '.join(wrong))

    camera = session.ask_ok('state camera')['state']['camera']
    response = session.ask('pose look-at ' + ' '.join(str(camera[axis]) for axis in 'xyz'))
    check('pose look-at the camera position fails and changes nothing',
          not response['ok'] and session.ask_ok('state camera')['state']['camera'] == camera, response.get('error'))

    session.ask_ok('pose ' + ' '.join(str(start[key]) for key in ['x', 'y', 'z', 'heading', 'pitch']))


def project_to_scene(camera, scene, position):
    """SceneEditor::ProjectToScene 처럼 world 위치를 client pixel 로 투영한다. camera 뒤면 None."""
    import numpy as np
    heading, pitch = float(camera['heading']), float(camera['pitch'])
    forward = np.array([-np.cos(pitch) * np.sin(heading), np.sin(pitch), -np.cos(pitch) * np.cos(heading)])
    right = np.array([np.cos(heading), 0.0, -np.sin(heading)])
    up = np.cross(right, forward)
    offset = position - vector_of(camera)
    depth = offset @ forward
    if depth < float(camera['near']):
        return None
    tangent = np.tan(float(camera['vertical_fov']) / 2)
    x = (offset @ right) / (depth * tangent * float(camera['aspect']))
    y = (offset @ up) / (depth * tangent)
    return np.array([scene['left'] + (0.5 + 0.5 * x) * scene['width'], scene['top'] + (0.5 - 0.5 * y) * scene['height']])


def check_editor(session, check, changes):
    """editor 명령이 icon click 과 W, E key 가 바꾸는 선택과 gizmo mode 를 바꾸고, state editor 의 icon 위치가 투영과 맞는지 본다.
    UI 를 숨긴 상태에서 부르고, 선택, mode, UI, camera 를 처음 상태로 되돌려 놓는다."""
    import numpy as np
    from PIL import Image

    def editor_state():
        return session.ask_ok('state editor')['state']['editor']

    usage = session.ask_ok('help')['commands']['editor']
    check('editor usage gives what it changes and the units', all(term in usage for term in ['select', 'mode', 'axes', 'client pixels', 'world position']), usage)

    original = editor_state()
    start = session.ask_ok('state camera')['state']['camera']
    light = vector_of(original['objects'][LIGHT_OBJECT]['position'])

    record_change(session, changes, f'editor select {LIGHT_OBJECT}', f'editor select {LIGHT_OBJECT}', 'editor')
    selected = editor_state()['selected']
    record_change(session, changes, 'editor select none', 'editor select none', 'editor')
    unselected = editor_state()['selected']
    before = editor_state()
    unknown = session.ask('editor select Moon')
    check('editor select picks the object or none, and an unknown name fails with the names and changes nothing',
          selected == LIGHT_OBJECT and unselected == 'none' and not unknown['ok'] and LIGHT_OBJECT in unknown.get('error', '') and editor_state() == before,
          f'{selected}, {unselected}, {unknown.get("error")}')

    session.ask_ok('ui show')
    session.ask_ok(f'editor select {LIGHT_OBJECT}')
    session.ask_ok('pose look-at ' + ' '.join(repr(float(value)) for value in light))
    session.ask_ok('frames 2')

    def gizmo_around_light(name):
        session.ask_ok('frames 2')
        icon = editor_state()['objects'][LIGHT_OBJECT]['screen']
        x, y = round(float(icon['x'])), round(float(icon['y']))
        image = np.asarray(Image.open(capture(session, 'display', name)))
        # ImGuizmo 의 gizmo 는 scene 높이의 몇 % 크기라 icon 을 중심으로 이 정도면 덮는다.
        return image[max(0, y - 150):y + 150, max(0, x - 150):x + 150]

    record_change(session, changes, 'editor mode rotate', 'editor mode rotate', 'editor')
    rotate_mode = editor_state()['gizmo_mode']
    rotate, rotate_again = gizmo_around_light('selftest-editor-rotate'), gizmo_around_light('selftest-editor-rotate-again')
    record_change(session, changes, 'editor mode translate', 'editor mode translate', 'editor')
    translate_mode = editor_state()['gizmo_mode']
    translate = gizmo_around_light('selftest-editor-translate')
    check('editor mode sets the gizmo mode, and the display around the light differs between the modes but not within one',
          rotate_mode == 'rotate' and translate_mode == 'translate' and np.array_equal(rotate, rotate_again) and not np.array_equal(rotate, translate),
          f'{rotate_mode}, {translate_mode}, rotate twice equal {np.array_equal(rotate, rotate_again)}')

    # 기본 light 의 축은 world 의 축과 다르므로 rotate gizmo 의 ring 이 다르게 놓인다.
    session.ask_ok('editor mode rotate')
    session.ask_ok('editor axes local')
    local = gizmo_around_light('selftest-editor-axes-local')
    record_change(session, changes, 'editor axes world', 'editor axes world', 'editor')
    world_axes = editor_state()['rotate_axes']
    world, world_again = gizmo_around_light('selftest-editor-axes-world'), gizmo_around_light('selftest-editor-axes-world-again')
    record_change(session, changes, 'editor axes local', 'editor axes local', 'editor')
    local_axes = editor_state()['rotate_axes']
    before = editor_state()
    unknown = session.ask('editor axes up')
    check('editor axes sets the axes of the rotate rings, the display around the light differs between them but not within one, '
          'and an unknown axes fails and changes nothing',
          world_axes == 'world' and local_axes == 'local' and np.array_equal(world, world_again) and not np.array_equal(local, world)
          and not unknown['ok'] and editor_state() == before,
          f'{world_axes}, {local_axes}, world twice equal {np.array_equal(world, world_again)}, {unknown.get("error")}')
    session.ask_ok('editor mode translate')

    # pose look-at light 뒤라 light 의 icon 이 scene 가운데에 있고, 다음 pose 에서는 가운데에서 벗어난다.
    camera = session.ask_ok('state camera')['state']['camera']
    poses = {'at the center': [camera[key] for key in ['x', 'y', 'z', 'heading', 'pitch']],
             'off the center': [camera['x'], camera['y'], camera['z'], float(camera['heading']) + 0.15, float(camera['pitch']) - 0.1]}
    off = []
    for name, pose in poses.items():
        session.ask_ok('pose ' + ' '.join(str(value) for value in pose))
        state = session.ask_ok('state')['state']
        icon, expected = state['editor']['objects'][LIGHT_OBJECT], project_to_scene(state['camera'], state['ui']['scene'], light)
        screen = np.array([float(icon['screen'][axis]) for axis in 'xy']) if 'screen' in icon else None
        if expected is None or screen is None or not icon['on_screen'] or np.abs(screen - expected).max() > 0.5:
            off.append(f'{name}: {icon} vs {None if expected is None else expected.tolist()}')
    session.ask_ok('pose look-at ' + ' '.join(repr(float(value)) for value in vector_of(camera) * 2 - light))
    hidden = editor_state()['objects'][LIGHT_OBJECT]
    check('state editor gives the icon where the camera projects the light, and no position behind the camera',
          not off and hidden['on_screen'] is False and 'screen' not in hidden, '; '.join(off) or str(hidden))

    session.ask_ok('editor select ' + original['selected'])
    session.ask_ok('editor mode ' + original['gizmo_mode'])
    session.ask_ok('editor axes ' + original['rotate_axes'])
    session.ask_ok('ui hide')
    session.ask_ok('pose ' + ' '.join(str(start[key]) for key in ['x', 'y', 'z', 'heading', 'pitch']))
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    check('editor and camera are back as they were', editor_state() == original and session.ask_ok('state camera')['state']['camera'] == start)


def check_fit_needs_a_capture(session, check):
    """validation fit 이 Fit to Capture 처럼 그 view 의 capture 가 없으면 맞추지 않는지 본다. 아무 capture 도 하기 전에 부른다."""
    section = 'Ray Tracing View'
    original = session.ask_ok(f'settings get {section}')['values']['View']
    session.ask_ok(f'settings set {section}: View=Validation: Depth Comparison')
    session.ask_ok(f'settings set {section}: Heatmap=Depth Error')
    before = session.ask_ok('state')['state']
    refused = session.ask('validation fit')
    unchanged = without_time_varying(session.ask_ok('state')['state']) == without_time_varying(before)
    check('validation fit fails without a capture of the view and changes nothing',
          not refused['ok'] and 'capture' in refused.get('error', '') and not before['validation']['captured'] and unchanged, refused.get('error'))
    session.ask_ok(f'settings set {section}: View={original}')
    session.ask_ok(f'frames {VALIDATION_VIEW_SETTLE_FRAMES}')


def check_validation_fit(session, check, changes):
    """validation fit 이 Fit to Capture 처럼 Heatmap Range 를 마지막 capture 에 맞추고, 버튼이 disabled 인 조건에서는 거절하는지 본다.
    Ray Tracing View 의 View 를 처음 값으로 되돌려 놓는다."""
    section = 'Ray Tracing View'
    path = f'settings.{section}'
    usage = session.ask_ok('help')['commands']['validation']
    check('validation usage gives what fit changes', all(term in usage for term in ['fit', 'Fit to Capture', 'Heatmap Range', 'p1 ~ p99']), usage)
    original = session.ask_ok(f'settings get {section}')['values']['View']

    def refused():
        before = session.ask_ok(f'state {path}')['state']
        reply = session.ask('validation fit')
        return not reply['ok'] and session.ask_ok(f'state {path}')['state'] == before, reply.get('error')

    session.ask_ok(f'settings set {section}: View=Off')
    not_validation = refused()
    session.ask_ok(f'settings set {section}: View=Validation: Depth Comparison')
    # Validation view 를 고르면 heatmap 은 첫 category 에서 시작한다.
    category = refused()
    check('validation fit fails where Fit to Capture is disabled and changes nothing', not_validation[0] and category[0],
          f'not a Validation view: {not_validation[1]}; a category heatmap: {category[1]}')

    start_range = '-3,3'
    session.ask_ok(f'settings set {section}: Heatmap=Depth Error; Heatmap Range={start_range}')
    session.ask_ok('frames 2')
    session.ask_ok('validation capture')
    fitted = record_change(session, changes, 'validation fit', 'validation fit', path)['state']['settings'][section]['Heatmap Range']
    again = session.ask_ok('validation fit')['state']['settings'][section]['Heatmap Range']
    check('validation fit fits the Heatmap Range to the capture and gives the same range again', fitted != start_range and again == fitted,
          f'{start_range} -> {fitted} -> {again}')
    session.ask_ok(f'settings set {section}: View={original}')
    session.ask_ok(f'frames {VALIDATION_VIEW_SETTLE_FRAMES}')


def depth_match_tolerance():
    match = re.search(r'^static const float kDepthMatchTolerance = ([0-9.e+-]+);', VALIDATION_SHADER.read_text(encoding='utf-8'), re.M)
    if match is None:
        raise HarnessError(f'{VALIDATION_SHADER} has no line starting with static const float kDepthMatchTolerance = <number>;')
    return float(match.group(1))


def raster_depth_of(z, camera):
    """Camera.cpp 의 reverse Z projection 을 거꾸로 풀어 depth buffer 값을 camera forward 방향의 거리로 되돌린다."""
    near, far = float(camera['near']), float(camera['far'])
    return near * far / (near + float(z) * (far - near))


def cell_of(pick):
    """단일 pick 의 응답을 grid_cells 의 칸 값처럼 쓴다."""
    if not pick['hit']:
        return {key: None for key in ['instance', 'material', 'distance', 'position']}
    return {'instance': str(pick['instance']), 'material': str(pick['material']), 'distance': pick['distance'],
            'position': ','.join(pick['position'][axis] for axis in 'xyz')}


def check_pick(session, check):
    """pick 이 본 것을 단일 pick, model bounds, raster depth, pose look-at 과 견준다. UI 를 숨긴 상태에서 부르고, camera 와 UI 를 되돌려 놓는다."""
    import numpy as np

    usage = session.ask_ok('help')['commands']['pick']
    check('pick usage gives the pixel convention and the units',
          all(term in usage for term in ['client pixels', 'sample', 'TAA', 'world units', 'reverse Z', 'alpha_test']), usage)

    state = session.ask_ok('state')['state']
    camera, scene = state['camera'], state['ui']['scene']
    before = read_dds_rgba32f(capture(session, 'scene', 'selftest-pick-before'))
    cells = grid_cells(session.ask_ok('pick grid 16x9')['grid'])
    picks = [session.ask_ok(f'pick {x} {y}')['pick'] for x, y, _ in cells]
    after = read_dds_rgba32f(capture(session, 'scene', 'selftest-pick-after'))
    check('scene is bit-identical before and after pick', np.array_equal(before.view(np.uint32), after.view(np.uint32)),
          describe_difference(before, after))

    low, high = (vector_of(state['model']['bounds'][end]) for end in ['min', 'max'])
    margin = 1e-5 * np.abs(high - low).max()
    differing = [f'{x},{y}: grid {values}, pick {cell_of(pick)}' for (x, y, values), pick in zip(cells, picks) if values != cell_of(pick)]
    outside = [f'{pick["pixel"]}: {pick["position"]}' for pick in picks
               if pick['hit'] and not np.all((vector_of(pick['position']) >= low - margin) & (vector_of(pick['position']) <= high + margin))]
    hits = sum(pick['hit'] for pick in picks)
    check('pick grid gives in each cell what pick gives at its center, and every hit is in the model bounds',
          not differing and not outside and hits > len(picks) // 2,
          '; '.join(differing[:3] + outside[:3]) or f'{hits} of {len(picks)} cells hit')

    # Depth Comparison validation 처럼, alpha test 가 ray 가 맞은 것을 정한 pixel 은 raster 가 다른 mip 을 읽어 다른 것을 볼 수 있으므로 뺀다.
    tolerance = depth_match_tolerance()
    compared, decided_by_alpha_test, mismatched, misdecoded = 0, 0, [], []
    for pick in picks:
        if not (pick['hit'] and pick['raster']['hit']):
            continue
        raster_depth = raster_depth_of(pick['raster']['z'], camera)
        if abs(float(pick['raster']['depth']) - raster_depth) > 1e-6 * raster_depth:
            misdecoded.append(f'{pick["pixel"]}: {pick["raster"]} vs {raster_depth}')
        if pick['alpha_test']:
            decided_by_alpha_test += 1
            continue
        compared += 1
        error = abs(float(pick['depth']) - raster_depth) / raster_depth
        if error > tolerance:
            mismatched.append(f'{pick["pixel"]}: depth {pick["depth"]}, raster {raster_depth}, error {error:.3g}')
    check('pick raster depth is its z decoded by the reverse Z of Camera.cpp', not misdecoded, '; '.join(misdecoded[:3]))
    check(f'pick depth is the raster depth within {tolerance:g} where alpha testing does not decide the hit',
          not mismatched and compared >= 0.9 * len(picks),
          '; '.join(mismatched[:3]) or f'{compared} of {len(picks)} cells compared, {decided_by_alpha_test} decided by alpha testing')

    # TAA 를 끄면 scene 의 가운데 pixel 의 sample 은 frustum 의 가운데라서, 그 ray 는 camera 의 forward 와 같다 (CameraRays.h).
    # scene 의 폭과 높이가 짝수일 때만 그렇다.
    center = (scene['left'] + scene['width'] // 2, scene['top'] + scene['height'] // 2)
    usable = [pick for pick in picks if pick['hit'] and not pick['alpha_test']]
    target = vector_of(max(usable, key=lambda pick: (pick['pixel']['y'], -abs(pick['pixel']['x'] - center[0])))['position'])
    session.ask_ok('pose look-at ' + ' '.join(repr(float(value)) for value in target))
    centered = session.ask_ok(f'pick {center[0]} {center[1]}')['pick']
    missed_by = np.linalg.norm(vector_of(centered['position']) - target) if centered['hit'] else None
    check('pick at the scene center after pose look-at a picked point hits that point',
          scene['width'] % 2 == 0 and scene['height'] % 2 == 0 and missed_by is not None and missed_by <= 1e-4 * float(centered['distance']),
          f'{missed_by} away from {target.tolist()} at distance {centered.get("distance")}')
    session.ask_ok('pose ' + ' '.join(str(camera[key]) for key in ['x', 'y', 'z', 'heading', 'pitch']))

    wrong = []
    before = without_time_varying(session.ask_ok('state')['state'])
    for line in ['pick', 'pick 1', 'pick 1.5 2', 'pick 1 2 3', 'pick grid 0x5', 'pick grid 65x64', 'pick grid 4x']:
        if session.ask(line)['ok']:
            wrong.append(line)
    check('malformed pick fails and changes nothing', not wrong and without_time_varying(session.ask_ok('state')['state']) == before,
          ', '.join(wrong))

    session.ask_ok('ui show')
    shown = session.ask_ok('state')['state']
    shown_scene = shown['ui']['scene']
    under_ui = [(5, 5), (shown_scene['left'] + shown_scene['width'] + 10, shown_scene['top'] + 100)]
    replies = [session.ask(f'pick {x} {y}') for x, y in under_ui]
    check('pick under the UI fails and changes nothing',
          not any(reply['ok'] for reply in replies) and without_time_varying(session.ask_ok('state')['state']) == without_time_varying(shown),
          '; '.join(reply.get('error', 'ok') for reply in replies))
    session.ask_ok('ui hide')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    check('camera and UI are back as they were after pick', session.ask_ok('state camera')['state']['camera'] == camera and
          session.ask_ok('state ui')['state']['ui'] == state['ui'])


def check_rest_of_ui_commands(session, check):
    """UI 조작을 대신하는 명령 중 앞의 check 가 보지 않은 것도, 그 조작이 바꾸는 state 를 바꾸고 바뀐 부분으로 응답하는지 본다.
    UI 를 숨긴 상태에서 부르고, 바꾼 것을 되돌려 놓는다."""
    changes = []
    change = functools.partial(record_change, session, changes)
    start = session.ask_ok('state')['state']

    client = start['ui']['client']
    change('window client', 'window client 1600x900', 'ui')
    change('window client back', f'window client {client["width"]}x{client["height"]}', 'ui')

    speed = start['camera']['move_speed']
    faster = float(speed) * 2
    change('settings set Editor Camera', f'settings set Editor Camera: Move Speed={faster:g}', 'settings.Editor Camera')
    moved_speed = session.ask_ok('state camera')['state']['camera']['move_speed']
    session.ask_ok(f'settings set Editor Camera: Move Speed={speed}')

    back_faces = start['settings']['Ray Tracing View']['Show Single-Sided Back Faces']
    for section, key, value in [('Ray Tracing View', 'Show Single-Sided Back Faces', '0' if back_faces == '1' else '1'),
                                ('Path Tracer', 'Samples per Frame', str(int(start['settings']['Path Tracer']['Samples per Frame']) + 1))]:
        change(f'settings set {section}', f'settings set {section}: {key}={value}', f'settings.{section}')
        session.ask_ok(f'settings set {section}: {key}={start["settings"][section][key]}')

    # Show 가 꺼져 있으면 reset 은 다시 켤 때 일어나므로, 켜고 쌓은 뒤에 reset 한다.
    session.ask_ok('settings set Path Tracer: Show=1')
    session.ask_ok('pt wait samples=2')
    change('pt reset', 'pt reset', 'pt')
    session.ask_ok(f'settings set Path Tracer: Show={start["settings"]["Path Tracer"]["Show"]}')

    line = session.ask_ok('describe')['line']
    camera = start['camera']
    session.ask_ok('pose ' + ' '.join(repr(float(camera[key]) + 10) for key in ['x', 'y', 'z']) + f' {camera["heading"]} {camera["pitch"]}')
    session.ask_ok('light set elevation=40')
    change('restore', f'restore {line}', 'scene', 'camera', 'light', 'sky', 'settings.Engine Tuning', 'settings.Path Tracer')
    # restore 는 줄의 pt samples 를 Target Samples 로 둔다.
    session.ask_ok(f'settings set Path Tracer: Target Samples={start["settings"]["Path Tracer"]["Target Samples"]}')

    check_changes(check, 'window, settings set Editor Camera, Ray Tracing View and Path Tracer, pt reset and restore change the state '
                         'and reply with the changed part', changes)
    check('settings set Editor Camera changes the move speed in state camera', float(moved_speed) == faster != float(speed),
          f'{speed} -> {moved_speed}')
    # pt 는 reset 과 Show 를 켜고 끈 것이 resets 를 늘린다.
    now = session.ask_ok('state')['state']
    differing = [key for key in start if key not in ('frame', 'pt') and now[key] != start[key]]
    check('the state other than pt is as it was after the rest of the UI commands', not differing, ', '.join(differing))


def check_profile(session, check, config):
    """profile 이 PT 를 그리는 동안 그 scope 의 GPU time 을 frame 수만큼 모으고, 없는 scope 와 그리지 않는 scope 는 거절하는지 본다.
    PT 의 Show 를 되돌려 놓는다."""
    show = session.ask_ok('state settings.Path Tracer')['state']['settings']['Path Tracer']['Show']
    unknown = session.ask('profile No Such Scope frames=1')
    check('profile refuses a scope the profiler does not have', not unknown['ok'], unknown.get('error'))

    session.ask_ok('settings set Path Tracer: Show=1')
    session.ask_ok('frames 2')
    measured = session.ask('profile Path Tracer frames=4')
    if config == 'Release':
        check('profile has no Path Tracer scope in Release, which compiles the timers out', not measured['ok'], measured.get('error'))
    else:
        gpu = measured.get('gpu_ms')
        check('profile gives the GPU time of Path Tracer over 4 frames',
              measured['ok'] and measured['frames'] == 4 and 0 < float(gpu['min']) <= float(gpu['mean']) <= float(gpu['max']), measured)
        session.ask_ok('settings set Path Tracer: Show=0')
        session.ask_ok('frames 2')
        stopped = session.ask('profile Path Tracer frames=2')
        check('profile fails for a scope that does not run', not stopped['ok'], stopped.get('error'))
    session.ask_ok(f'settings set Path Tracer: Show={show}')


def check_acceptance(session, check):
    """ACCEPTANCE_SCENARIO 를 run 과 같은 방식으로 실행하고, 단계마다 이름을 붙인 응답으로 판정한다. 요청한 값은 scenario 에서 읽는다.
    UI 를 숨긴 상태에서 부르고, camera, light, editor 를 되돌려 놓는다."""
    import numpy as np

    lines = scenario_lines(ACCEPTANCE_SCENARIO)

    def argument_of(prefix):
        return next(line[len(prefix):] for line in lines if line.startswith(prefix))

    restore_line = session.ask_ok('describe')['line']
    before = session.ask_ok('state')['state']
    responses, failed = run_lines(session, lines, echo=False)
    try:
        if not check('the acceptance scenario runs to the end', failed is None, failed['error'] if failed else ''):
            return

        requested = dict(item.split('=') for item in argument_of('light set ').split())
        lit = responses['lit']['state']
        light, saved = lit['light'], numbers_of(lit['settings'][LIGHT_SECTION]['Direction'])
        check('acceptance 1: state light is at the elevation and azimuth light set asked for, and editor.ini keeps that direction',
              abs(float(light['elevation']) - float(requested['elevation'])) <= 1e-4
              and degrees_between(float(light['azimuth']), float(requested['azimuth'])) <= 1e-4
              and np.array_equal(saved.astype(np.float32), vector_of(light['direction']).astype(np.float32))
              and light != responses['light_before']['state']['light'],
              f'asked {requested}; elevation {light["elevation"]}, azimuth {light["azimuth"]}, direction {light["direction"]}, saved {saved.tolist()}')

        wanted = {'selected': argument_of('editor select '), 'gizmo_mode': argument_of('editor mode ')}
        editor = responses['selection']['state']['editor']
        shot = responses['gizmo']
        saved = json.loads(Path(shot['state']).read_text(encoding='utf-8'), parse_float=str)
        check('acceptance 2: state editor and the state file of the display shot have the selection and gizmo mode',
              all(editor[key] == value and saved['editor'][key] == value for key, value in wanted.items()) and saved['frame'] == shot['frame'],
              f'state editor {editor["selected"]}, {editor["gizmo_mode"]}; frame {saved["frame"]} file {saved["editor"]["selected"]}, '
              f'{saved["editor"]["gizmo_mode"]}')

        section = 'Ray Tracing View'
        unfitted = responses['unfitted']['state']['settings'][section]['Heatmap Range']
        fitted = responses['fitted']['state']['settings'][section]['Heatmap Range']
        check('acceptance 3: validation fit changes the Heatmap Range', fitted != unfitted, f'{unfitted} -> {fitted}')

        floor, center = responses['floor']['pick'], responses['center']['pick']
        missed_by = np.linalg.norm(vector_of(center['position']) - vector_of(floor['position'])) if floor['hit'] and center['hit'] else None
        check('acceptance 4: the picked point faces up, and the pick at the scene center after pose look-at hits it',
              floor['hit'] and not floor['alpha_test'] and float(floor['normal']['y']) > 0.99
              and missed_by is not None and missed_by <= 1e-4 * float(center['distance']),
              f'floor {floor.get("position")} normal {floor.get("normal")}; center {missed_by} away at distance {center.get("distance")}')

        cells = grid_cells(responses['grid']['grid'])
        on_floor = sum(values['material'] == str(floor['material']) for _, _, values in cells)
        check('acceptance 5: most cells of pick grid are the material of the floor', on_floor > len(cells) / 2,
              f'{on_floor} of {len(cells)} cells are material {floor["material"]}')
    finally:
        session.ask_ok(f'restore {restore_line}')
        session.ask_ok(f'editor select {before["editor"]["selected"]}')
        session.ask_ok(f'editor mode {before["editor"]["gizmo_mode"]}')
        session.ask_ok(f'settings set Ray Tracing View: View={before["settings"]["Ray Tracing View"]["View"]}')
        session.ask_ok('ui hide' if not before['ui']['visible'] else 'ui show')
        session.ask_ok(f'frames {VALIDATION_VIEW_SETTLE_FRAMES}')


def check_pick_known_answers(args, check):
    """pickscene.py 의 정답을 아는 test scene 을 띄워, pick 이 돌려준 것을 해석적으로 구한 정답과 비교한다."""
    import numpy as np
    import pickscene

    scene_file = write_scene_file(pickscene.write(HARNESS_DIR / PICK_SCENE))
    instance = start_instance(args.config, scene=scene_file, build_first=False)
    session = Session(instance)
    try:
        session.ask_ok('ui hide')
        session.ask_ok(f'frames {SETTLE_FRAMES}')
        state = session.ask_ok('state')['state']
        camera, scene = state['camera'], state['ui']['scene']
        transform = pickscene.world_transform(state['model']['bounds'])
        if not check('the pick test scene is opened and scaled by one factor', state['scene']['path'] == scene_file and transform is not None,
                     f'{state["model"]}'):
            return
        forward = vector_of(camera['forward'])

        def expect(sample):
            origin, direction = camera_ray(camera, scene, sample)
            cosine = direction @ forward
            return origin, direction, pickscene.classify(origin, direction, transform, float(camera['near']) / cosine, float(camera['far']) / cosine)

        def kind(found):
            return None if found is None else (found[1].name, found[2].name)

        def unambiguous(pixel):
            return is_unambiguous(lambda sample: kind(expect(sample)[2]), pixel)

        grid = grid_cells(session.ask_ok('pick grid 32x18')['grid'])
        wrong, judged, instances = [], {}, {}
        for x, y, values in grid:
            if not unambiguous((x, y)):
                continue
            origin, direction, found = expect(np.array([x, y]) + 0.5)
            name = 'sky' if found is None else found[1].name
            judged[name] = judged.get(name, 0) + 1
            if found is None:
                if values['material'] is not None:
                    wrong.append(f'{x},{y}: {values} where nothing is')
                continue
            t, surface, face = found
            if values['material'] != str(surface.material):
                wrong.append(f'{x},{y}: {values} where {surface.name} {face.name} is at {t}')
                continue
            position = np.array([float(value) for value in values['position'].split(',')])
            if abs(float(values['distance']) - t) > 1e-4 * t or np.linalg.norm(position - (origin + t * direction)) > 1e-4 * t:
                wrong.append(f'{x},{y}: {values} where {surface.name} {face.name} is at {t}')
            instances.setdefault(surface.name, set()).add(values['instance'])
        distinct_instances = all(len(ids) == 1 for ids in instances.values()) and len(set().union(*instances.values())) == len(instances)
        check('pick grid finds the known surface, distance and position in each cell of the test scene',
              not wrong and distinct_instances and all(judged.get(name, 0) >= 20 for name in ['wall', 'box', 'sky']),
              '; '.join(wrong[:3]) or f'judged cells {judged}, instances {instances}')

        middle = np.array([scene['left'] + scene['width'] / 2, scene['top'] + scene['height'] / 2])

        # 상자의 옆면처럼 grid 의 칸에 들지 않을 만큼 좁은 면도 고르도록 grid 보다 촘촘한 pixel 에서 찾는다.
        lattice = [(x, y) for y in range(scene['top'], scene['top'] + scene['height'], 16) for x in range(scene['left'], scene['left'] + scene['width'], 16)]
        lattice.sort(key=lambda pixel: np.linalg.norm(np.array(pixel) - middle))

        def representative(seen):
            """scene 가운데에 가장 가까운, seen (면의 kind 나 하늘의 None) 만 보는 pixel."""
            return next((pixel for pixel in lattice if kind(expect(np.array(pixel) + 0.5)[2]) == seen and unambiguous(pixel)), None)

        wrong = []
        for seen in [('wall', 'front'), ('box', '+z'), ('box', '-x'), None]:
            pixel = representative(seen)
            if pixel is None:
                wrong.append(f'no cell sees {seen or "the sky"}')
                continue
            pick = session.ask_ok(f'pick {pixel[0]} {pixel[1]}')['pick']
            sample = np.array([float(pick['sample'][axis]) for axis in 'xy'])
            origin, direction, found = expect(sample)
            problems = [] if np.array_equal(sample, np.array(pixel) + 0.5) else [f'sample {sample.tolist()} is not the pixel center']
            if found is None:
                problems += [f'{key} {pick[key]}' for key in ['hit', 'alpha_test'] if pick[key]]
                problems += ['raster hit'] if pick['raster']['hit'] else []
            elif not pick['hit']:
                problems.append('no hit')
            else:
                t, surface, face = found
                depth = t * (direction @ forward)
                expected = {'material': surface.material, 'front_face': True, 'alpha_test': False}
                problems += [f'{key} {pick[key]}' for key, value in expected.items() if pick[key] != value]
                if abs(float(pick['distance']) - t) > 1e-4 * t or abs(float(pick['depth']) - depth) > 1e-4 * depth:
                    problems.append(f'distance {pick["distance"]}, depth {pick["depth"]} for {t}, {depth}')
                if np.abs(vector_of(pick['normal']) - np.array(face.normal)).max() > 1e-4:
                    problems.append(f'normal {pick["normal"]} for {face.normal}')
                base_color = np.array([float(pick['base_color'][channel]) for channel in 'rgba'])
                if np.abs(base_color - np.array(surface.base_color)).max() > 1e-3:
                    problems.append(f'base color {base_color.tolist()} for {surface.base_color}')
                if not pick['raster']['hit'] or abs(raster_depth_of(pick['raster']['z'], camera) - depth) > depth_match_tolerance() * depth:
                    problems.append(f'raster {pick["raster"]} for depth {depth}')
            if problems:
                wrong.append(f'{seen or "sky"} at {pixel}: ' + ', '.join(problems))
        check('pick gives the known sample, distance, depth, normal, material, base color and raster depth on the wall, two box faces and the sky',
              not wrong, '; '.join(wrong))

        check('the test scene camera did not move while picking', session.ask_ok('state camera')['state']['camera'] == camera)
        if args.config == 'Debug':
            d3d12 = session.ask_ok('status')['d3d12_messages']
            check('no D3D12 corruption, error or warning while picking the test scene',
                  d3d12['corruption'] == d3d12['error'] == d3d12['warning'] == 0, d3d12)
    finally:
        session.close()
        stopped = stop_instance(instance)
    check('the test scene exits normally', stopped['result'] == 'quit' and stopped['exit_reason'] == 'normal',
          f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    if args.config == 'Debug':
        check_nothing_outlives_device(check, Path(instance['log']))


def check_nothing_outlives_device(check, log):
    at_exit = summarize_log(log)['d3d12']['at_exit'] or {}
    names = d3d12_message_names()
    check(f'the D3D12 exit report has only one #{LIVE_DEVICE_ID} LIVE_DEVICE',
          {id: message['count'] for id, message in at_exit.items()} == {LIVE_DEVICE_ID: 1},
          ', '.join(f'#{id} {names.get(id, "unknown")} x{message["count"]}' for id, message in sorted_by_severity(at_exit)) or 'none')


def selftest_running_app(args, check):
    from PIL import Image
    import numpy as np

    # Debug 밖의 config 는 -debug 1 없이 띄우므로 debug layer 가 꺼져 있다 (MiniEngine/readme.md).
    logs_d3d12 = args.config == 'Debug'
    repeated_faults = logged_lines_per_d3d12_id() + 1
    result = build(args.config)
    check(f'build {args.config} exits with 0', result['ok'], '; '.join(result['errors']))
    exe = exe_path(args.config)
    if not check('exe is newer than the sources', exe.exists() and exe.stat().st_mtime > newest_source_mtime()):
        return

    # 정리하지 않으면 그대로 남도록, 이름이 실제 run 보다 앞서는 빈 run 을 KEPT_RUNS 개보다 많이 만든다.
    # 이름 순서로 가장 오래된 것들이라 실제 run 은 이 검사가 없을 때와 똑같이 정리된다.
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    stale_runs = [RUNS_DIR / f'00000000-{index:06d}' for index in range(KEPT_RUNS + 1)]
    for run in stale_runs:
        run.mkdir(exist_ok=True)
    instance = start_instance(args.config, build_first=False)
    remaining = [run for run in stale_runs if run.exists()]
    check(f'start deletes all but the newest {KEPT_RUNS} runs', not remaining and Path(instance['run_dir']).exists(),
          f'{len(remaining)} of {len(stale_runs)} older empty runs remain')
    session = Session(instance)
    try:
        status = session.ask_ok('status')
        docked_scene = status['scene']
        check('client is 1920x1080', status['client'] == '1920x1080', status['client'])
        check('hdr_output is off', status['hdr_output'] is False,
              'on an HDR display' if status['hdr_display'] else 'on an SDR display, so this does not show that -hdr_output 0 works')
        check(f'GPU is {args.gpu}', args.gpu in status['gpu'], status['gpu'])
        commands = session.ask_ok('help')['commands']
        check('help lists ping', 'ping' in commands, ', '.join(sorted(commands)))

        shown = capture(session, 'display', 'selftest-ui-shown')
        session.ask_ok('ui hide')
        status = session.ask_ok('status')
        check('scene is 1920x1080 after ui hide', status['scene'] == '1920x1080' and status['ui'] == 'hidden',
              f'scene {status["scene"]}, ui {status["ui"]}')
        hidden = capture(session, 'display', 'selftest-ui-hidden')
        sizes = [Image.open(path).size for path in (shown, hidden)]
        check('display PNG is the client size', all(size == (1920, 1080) for size in sizes), sizes)
        check('display differs with and without UI',
              not np.array_equal(np.asarray(Image.open(shown)), np.asarray(Image.open(hidden))))
        session.ask_ok('ui show')
        status = session.ask_ok('status')
        # panel 이 dock 에서 떨어지면 scene 이 client 폭 전체가 된다.
        check('scene is the docked size again after ui hide and ui show', status['scene'] == docked_scene,
              f'{docked_scene} at start, {status["scene"]} now')
        session.ask_ok('ui hide')

        key = 'Graphics/HDR/Exposure'
        original = session.ask_ok('settings get Engine Tuning')['values'][key]
        applied = session.ask_ok(f'settings set Engine Tuning: {key}=3')['values'][key]
        read = session.ask_ok('settings get Engine Tuning')['values'][key]
        check('settings set then get returns the value', applied == read == '3', f'set returned {applied}, get returned {read}')
        session.ask_ok(f'settings set Engine Tuning: {key}={original}')
        wrong = session.ask('settings set Engine Tuning: No/Such/Key=1')
        check('settings set with an unknown key fails', not wrong['ok'], wrong.get('error'))
        for section, assignment in [('Engine Tuning', f'{key}=abc'), ('Engine Tuning', 'Timing/VSync=maybe'),
                                    ('Ray Tracing View', 'View=Bogus')]:
            values_before = session.ask_ok(f'settings get {section}')['values']
            rejected = session.ask(f'settings set {section}: {assignment}')
            unchanged = session.ask_ok(f'settings get {section}')['values'] == values_before
            check(f'settings set {assignment} fails and changes nothing', not rejected['ok'] and unchanged,
                  f'ok {rejected["ok"]}, unchanged {unchanged}, rejected {rejected.get("rejected")}')

        session.ask_ok(f'frames {SETTLE_FRAMES}')
        settled = [read_dds_rgba32f(capture(session, 'scene', f'selftest-scene-{index}')) for index in (1, 2)]
        check('capture scene twice is bit-identical', np.array_equal(settled[0].view(np.uint32), settled[1].view(np.uint32)),
              describe_difference(settled[0], settled[1]))

        pose_keys = ['x', 'y', 'z', 'heading', 'pitch']
        start = session.ask_ok('pose')
        moved = [float(start[key]) + offset for key, offset in zip(pose_keys, [123.4, 56.7, -89.1, 0.3, -0.2])]
        first = session.ask_ok('pose ' + ' '.join(repr(value) for value in moved))
        check('pose sets the camera to the given values',
              all(np.float32(value) == np.float32(float(first[key])) for key, value in zip(pose_keys, moved)),
              ' '.join(first[key] for key in pose_keys))
        second = session.ask_ok('pose ' + ' '.join(first[key] for key in pose_keys))
        check('pose round-trips with %.9g', all(first[key] == second[key] for key in pose_keys),
              ' '.join(first[key] for key in pose_keys))

        before = session.ask_ok('describe')['line']
        session.ask_ok('frames 60')
        after = session.ask_ok('describe')['line']
        check('describe stays the same for 60 frames', before == after, before if before == after else f'{before} -> {after}')

        session.ask_ok('pose ' + ' '.join(start[key] for key in pose_keys))
        restored = session.ask_ok(f'restore {before}')['line']
        check('describe, pose change, restore gives the first describe', restored == before == session.ask_ok('describe')['line'], restored)

        def check_same_as_settled(name, label):
            scene = read_dds_rgba32f(capture(session, 'scene', name))
            check(label, np.array_equal(settled[0].view(np.uint32), scene.view(np.uint32)), describe_difference(settled[0], scene))

        session.ask_ok('pose ' + ' '.join(start[key] for key in pose_keys))
        session.ask_ok(f'frames {SETTLE_FRAMES}')
        check_same_as_settled('selftest-scene-returned', 'scene at the first pose again after moving is bit-identical')

        taa = 'Graphics/AA/TAA/Enable'
        session.ask_ok(f'settings set Engine Tuning: {taa}=on')
        session.ask_ok('frames 30')
        with_taa = read_dds_rgba32f(capture(session, 'scene', 'selftest-scene-taa-on'))
        # 이 검사가 없으면 TAA 가 켜지지 않았어도 다음 검사가 통과한다.
        check('scene differs with TAA on', not np.array_equal(settled[0].view(np.uint32), with_taa.view(np.uint32)))
        session.ask_ok(f'settings set Engine Tuning: {taa}=off')
        session.ask_ok(f'frames {SETTLE_FRAMES}')
        check_same_as_settled('selftest-scene-taa-off', 'scene after TAA on for 30 frames and off again is bit-identical')

        sharpness = 'Graphics/AA/TAA/Sharpness'
        original_sharpness = session.ask_ok('settings get Engine Tuning')['values'][sharpness]
        session.ask_ok(f'settings set Engine Tuning: {sharpness}=0')
        session.ask_ok(f'frames {SETTLE_FRAMES}')
        check_same_as_settled('selftest-scene-sharpness-0', 'scene with TAA off does not change with Sharpness')
        session.ask_ok(f'settings set Engine Tuning: {sharpness}={original_sharpness}')

        check_fit_needs_a_capture(session, check)
        check_state(session, check)
        changes = []
        check_light(session, check, changes)
        check_look_at(session, check, changes)
        check_editor(session, check, changes)
        check_validation_fit(session, check, changes)
        check_changes(check, 'light set, pose look-at, editor and validation fit change the state and reply with the changed part', changes)
        check_pick(session, check)
        check_rest_of_ui_commands(session, check)
        check_acceptance(session, check)
        check_profile(session, check, args.config)

        abandoned = Channel(instance['pipe'], 10)
        abandoned.send('frames 1000000')
        gave_up = abandoned.receive(1.0) is None
        abandoned.close()
        started = time.monotonic()
        try:
            session.ask_ok('status', timeout=10)
            answered = True
        except HarnessError:
            answered = False
        check('a command its client gave up on does not block the next command', gave_up and answered,
              f'next command answered {answered} after {time.monotonic() - started:.1f} s')

        d3d12 = session.ask_ok('status')['d3d12_messages']
        if logs_d3d12:
            check('status reports D3D12 messages logged', d3d12['logged'] is True, d3d12)
            check('no D3D12 corruption, error or warning while running',
                  d3d12['corruption'] == d3d12['error'] == d3d12['warning'] == 0,
                  '; '.join(describe_d3d12(summarize_log(Path(instance['log']))['d3d12'])))
            before_fault = read_dds_rgba32f(capture(session, 'scene', 'selftest-scene-before-d3d12-error'))
            session.ask_ok('log mark d3d12-fault')
            session.ask_ok('fault d3d12_error')
            session.ask_ok('frames 2')
            errors = session.ask_ok('status')['d3d12_messages']['error']
            check('fault d3d12_error raises the error count in status', errors > d3d12['error'], f'{d3d12["error"]} -> {errors}')
            after_fault = read_dds_rgba32f(capture(session, 'scene', 'selftest-scene-after-d3d12-error'))
            check('scene after fault d3d12_error is bit-identical', np.array_equal(before_fault.view(np.uint32), after_fault.view(np.uint32)),
                  describe_difference(before_fault, after_fault))
            # 같은 ID 의 줄 수 제한을 넘겨, 줄로 쓰지 않은 것까지 세는지 본다.
            for _ in range(repeated_faults):
                session.ask_ok('fault d3d12_error')
            session.ask_ok('frames 2')
            repeated = session.ask_ok('status')['d3d12_messages']['error']
            check(f'status counts {repeated_faults} more faults', repeated == errors + repeated_faults, f'{errors} -> {repeated}')
        else:
            check('status reports D3D12 messages not logged because the debug layer is off',
                  d3d12 == {'logged': False, 'reason': 'debug layer off'}, d3d12)
            refused = session.ask('fault d3d12_error')
            check('fault d3d12_error fails when D3D12 messages are not logged', not refused['ok'], refused.get('error'))
    finally:
        session.close()

    summary = summarize_log(Path(instance['log']))
    check('log has RayQuery smoke test: PASS', summary['smoke_test'] == 'PASS', summary['smoke_test'])
    started = time.monotonic()
    stopped = stop_instance(instance)
    check(f'quit exits in {EXIT_TIMEOUT} s', stopped['result'] == 'quit' and stopped['exit_reason'] == 'normal',
          f'{stopped["result"]}, exit reason={stopped["exit_reason"]}, {time.monotonic() - started:.1f} s')

    text = summarize_log(Path(instance['log']))['text']
    stop_lines = describe_d3d12(stopped['d3d12'])
    if logs_d3d12:
        mark = text.find('[Harness] mark d3d12-fault')
        exit_report = text.find('D3D12 live objects at exit:')
        # 종료 report 에 ERROR 가 있어도 맞지 않게 mark 와 종료 report 사이에서 찾는다.
        fault_error = re.compile(r'^D3D12 ERROR #(\d+) ', re.MULTILINE).search(text, mark, exit_report) if 0 <= mark < exit_report else None
        check('log has a D3D12 ERROR between the mark and the exit report', fault_error is not None, stop_lines[0])
        if fault_error is not None:
            id, faults = fault_error[1], 1 + repeated_faults
            running_text = text[mark:exit_report]
            cap = re.search(rf'^D3D12 #{id}: logged (\d+) times, the rest are only counted$', running_text, re.MULTILINE)
            lines = len(re.findall(rf'^D3D12 ERROR #{id} ', running_text, re.MULTILINE))
            total = re.search(rf'^D3D12 #{id} total=(\d+)$', text, re.MULTILINE)
            counted = any(re.match(rf'  ERROR #{id} \w+ x{faults},', line) for line in stop_lines)
            check(f'{faults} faults of #{id} leave a cap line, total={faults} and x{faults} in stop',
                  cap is not None and lines == int(cap[1]) and total is not None and int(total[1]) == faults and counted,
                  f'{lines} lines, cap {cap[0] if cap else None}, {total[0] if total else None}; ' + '; '.join(stop_lines))
        # 종료 report 가 log 에 닿는지를 그 report 의 LIVE_DEVICE 로 본다.
        check(f'stop reports #{LIVE_DEVICE_ID} LIVE_DEVICE at exit',
              any(re.match(rf'  at exit: \w+ #{LIVE_DEVICE_ID} LIVE_DEVICE ', line) for line in stop_lines), '; '.join(stop_lines))
        check_nothing_outlives_device(check, Path(instance['log']))
        check('the D3D12 exit report comes before exit reason=normal', 0 <= exit_report < text.find('exit reason=normal'))
    else:
        check('stop reports D3D12 messages not logged', stop_lines == ['d3d12 messages: not logged (debug layer off)'], stop_lines)

    check_pick_known_answers(args, check)

    expected_reasons = {'assert': 'breakpoint at=', 'exception': 'exception code=0xC0000005'}
    for fault, expected in expected_reasons.items():
        instance = start_instance(args.config, build_first=False)
        message = ''
        try:
            with running_session() as crashing:
                crashing.ask_ok(f'fault {fault}')
                crashing.ask_ok('frames 30')
        except HarnessError as error:
            message = str(error)
        exited = wait_for_exit(instance['pid'], EXIT_TIMEOUT)
        if not exited:
            kill(instance['pid'])
        summary = summarize_log(Path(instance['log']))
        reason = summary['exit_reason'] or ''
        logged = reason.startswith(expected) and (fault != 'assert' or 'Assertion failed' in summary['text'])
        reported = f'exit reason={expected}' in message and instance['log'] in message
        check(f'fault {fault} exits without a dialog, logs the reason and do reports it', exited and logged and reported, message)
        INSTANCE_FILE.unlink(missing_ok=True)

    instance = start_instance(args.config, build_first=False)
    message = ''
    try:
        with running_session() as killed:
            kill(instance['pid'])
            killed.ask_ok('status')
    except HarnessError as error:
        message = str(error)
    check('an exit without a reason line reports the exit code', 'exit code 0x00000001' in message, message)
    INSTANCE_FILE.unlink(missing_ok=True)

    instance = start_instance(args.config, build_first=False)
    with running_session() as hanging:
        hanging.ask_ok('fault hang')
    message = ''
    try:
        with running_session() as hung:
            hung.ask('status')
    except HarnessError as error:
        message = str(error)
    check('do reports a hang and leaves the app running', 'hung' in message and is_running(instance), message)
    started = time.monotonic()
    stopped = stop_instance(instance)
    check('stop catches the hang by ping and kills', stopped['result'].startswith('killed: hang') and not is_running(instance),
          f'{stopped["result"]} after {time.monotonic() - started:.1f} s')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)

    def add_config(command):
        command.add_argument('--config', choices=CONFIGS, default=DEFAULT_CONFIG)

    def add_command(name, summary, handler):
        # 같은 문장을 viewer.py --help 의 subcommand 목록과 그 subcommand 의 --help 에 함께 보인다.
        command = commands.add_parser(name, help=summary, description=summary)
        command.set_defaults(handler=handler)
        return command

    command = add_command('build', 'build ModelViewer and print only the errors', command_build)
    add_config(command)

    command = add_command('start', 'build, then start ModelViewer in a new run folder under MiniEngine/ModelViewer/Harness/runs '
                                   "with a copy of scripts/harness-baseline as -settings_dir, so the user's editor.ini and imgui.ini "
                                   f'stay untouched. Deletes all but the newest {KEPT_RUNS} run folders, keeping the running one', command_start)
    add_config(command)
    command.add_argument('--scene', help='scene file path relative to MiniEngine/ModelViewer, passed as -scene. '
                                          'Without it the app opens the scene that kDefaultScene in ModelViewer.cpp names')
    command.add_argument('--no-build', action='store_true')
    command.add_argument('--gpu-validation', action='store_true',
                         help='pass -gpu_debug 1 for GPU-based validation, which checks on the GPU the state and descriptor of each '
                              'resource a shader accesses. Without it, the state of a resource a shader reads is not checked. '
                              'Debug only. Slower')

    command = add_command('do', 'send commands to the running instance one by one and stop at the first error. '
                                'Reports a crash or a hang but leaves a hung app to stop. "do help" lists the commands', command_do)
    command.add_argument('commands', nargs='+', metavar='command')

    command = add_command('run', 'send the commands in a file, one per line, as do does. A line starting with # is a comment. '
                                 '"<name> = <command>" names the response, and {<name>.<key>.<key>} in a later line becomes a value in it, '
                                 'as in "pose look-at {floor.pick.position.x} ...". {shots} is the folder where shot writes, '
                                 'as in "capture display {shots}/name". '
                                 'do takes the same lines', command_run)
    command.add_argument('file')

    command = add_command('shot', 'capture a target and write a small PNG to look at, '
                                  'with the state of the captured frame as JSON next to it', command_shot)
    command.add_argument('target', choices=['scene', 'overlay', 'display', 'pt'])
    command.add_argument('--crop', type=parse_crop, metavar='X,Y,W,H', help='in original pixels')
    command.add_argument('--max-width', type=int, default=PREVIEW_MAX_WIDTH)
    command.add_argument('--exposure', type=float, default=PREVIEW_EXPOSURE, help='for the scene and pt views only')

    command = add_command('stop', 'quit the running instance, killing it if it hangs, and summarize its log with the D3D12 messages. '
                                  'Exits with 1 unless the app ended normally, whatever the messages', command_stop)
    command.add_argument('--kill', action='store_true', help='kill without sending quit')

    command = add_command('snapshot', 'take the scene and display of fixed poses with the harness baseline and with the app defaults '
                                      '(TAA, motion blur, adaptive exposure) before changing the code, then compare the same shots after it',
                          None)
    actions = command.add_subparsers(dest='action', required=True)
    action = actions.add_parser('save', help=f'start a new instance and keep the shots in {SNAPSHOT_DIR}/<name>',
                                description=f'start a new instance and keep the shots in {SNAPSHOT_DIR}/<name>')
    action.set_defaults(handler=command_snapshot_save)
    action.add_argument('name')
    action.add_argument('--replace', action='store_true', help='take the snapshot again under the same name')
    add_config(action)
    action.add_argument('--no-build', action='store_true')
    action = actions.add_parser('compare', help='take the same shots in a new instance and report the change of each shot, '
                                                'with a sheet of before, after and the changed pixels. '
                                                'Exits with 1 if the app did not end normally, logged a D3D12 corruption, error or warning, '
                                                'or drew NaN or Inf',
                                description='take the same shots in a new instance and report the change of each shot, '
                                            'with a sheet of before, after and the changed pixels. '
                                            'Exits with 1 if the app did not end normally, logged a D3D12 corruption, error or warning, '
                                            'or drew NaN or Inf')
    action.set_defaults(handler=command_snapshot_compare)
    action.add_argument('name')
    add_config(action)
    action.add_argument('--no-build', action='store_true')

    command = add_command('selftest', 'check the harness itself', command_selftest)
    add_config(command)
    command.add_argument('--gpu', default='RTX 4060', help='a part of the GPU name that status must report')

    args = parser.parse_args()
    try:
        return args.handler(args)
    except (HarnessError, ImageFileError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
