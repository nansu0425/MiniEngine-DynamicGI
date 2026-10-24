"""ModelViewer 를 build 하고 띄워서 control channel 로 조작하고 확인하는 harness.

app 명령의 사용법은 `do help` 가 app 에서 받아 온다. app flag 는 MiniEngine/readme.md 에 있다.
"""

import argparse
import contextlib
import ctypes
import functools
import hashlib
import json
import msvcrt
import os
import re
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

from imagefiles import ImageFileError, read_dds_rgba32f

REPO_ROOT = Path(__file__).resolve().parent.parent
ENGINE_DIR = REPO_ROOT / 'MiniEngine'
VIEWER_DIR = ENGINE_DIR / 'ModelViewer'
HARNESS_DIR = VIEWER_DIR / 'Harness'
RUNS_DIR = HARNESS_DIR / 'runs'
INSTANCE_FILE = HARNESS_DIR / 'instance.json'
BASELINE_DIR = REPO_ROOT / 'scripts' / 'harness-baseline'
USER_SETTINGS = [VIEWER_DIR / 'editor.ini', VIEWER_DIR / 'imgui.ini']
MODEL_VIEWER_SOURCE = VIEWER_DIR / 'ModelViewer.cpp'
D3D12_MESSAGE_LOG_HEADER = ENGINE_DIR / 'Core' / 'Automation' / 'D3D12MessageLog.h'
# D3D12 message ID 의 이름을 읽을 Agility SDK 의 version 이 여기에 있다.
PACKAGES_CONFIG = ENGINE_DIR / 'Core' / 'packages.config'
SOURCE_DIRS = [ENGINE_DIR / 'Core', ENGINE_DIR / 'Model']
SOURCE_SUFFIXES = {'.cpp', '.h', '.inl', '.hlsl', '.hlsli'}

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


def default_assets():
    match = re.search(r'^const wchar_t kDefaultModel\[\] = L"([^"]+)";', MODEL_VIEWER_SOURCE.read_text(encoding='utf-8'), re.M)
    if match is None:
        raise HarnessError(f'{MODEL_VIEWER_SOURCE} has no line starting with '
                           'const wchar_t kDefaultModel[] = L"<glTF path relative to MiniEngine/ModelViewer>";')
    return [VIEWER_DIR / match.group(1), VIEWER_DIR / 'Textures']


def start_instance(config, model=None, build_first=True, gpu_validation=False):
    missing = [path for path in default_assets() if not path.exists()] if model is None else \
        [path for path in [VIEWER_DIR / model] if not path.exists()]
    if missing:
        hint = ' Run .\\scripts\\fetch-assets.ps1.' if model is None else ''
        raise HarnessError(f'missing {", ".join(str(path) for path in missing)}.{hint}')

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
    for baseline in BASELINE_DIR.glob('*.ini'):
        shutil.copy2(baseline, run_dir / baseline.name)
    prune_runs()

    log = run_dir / 'app.log'
    pipe = f'ModelViewerHarness-{run_dir.name}'
    command = [str(exe), '-settings_dir', str(run_dir), '-log', str(log), '-unattended', '1', '-control', pipe, '-hdr_output', '0']
    if model is not None:
        command += ['-model', model]
    if gpu_validation:
        command += ['-gpu_debug', '1']
    process = launch(command, VIEWER_DIR)

    instance = {'pid': process.pid, 'exe': str(exe), 'config': config, 'model': model, 'pipe': pipe,
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
    errors = sorted({line.strip() for line in completed.stdout.splitlines() if re.search(r': (fatal )?error ', line)})
    return {'ok': completed.returncode == 0, 'seconds': time.monotonic() - started, 'errors': errors, 'log': str(log)}


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
    if target == 'scene':
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


def capture(session, target, name):
    shots = Path(session.instance['run_dir']) / 'shots'
    shots.mkdir(exist_ok=True)
    response = session.ask_ok(f'capture {target} {shots / name}')
    return Path(response['path'])


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


def run_commands(lines):
    with running_session() as session:
        for line in lines:
            print(f'> {line}')
            response = session.ask(line)
            print(json.dumps(response, ensure_ascii=False))
            if not response['ok']:
                return 1
    return 0


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
    instance = start_instance(args.config, args.model, not args.no_build, args.gpu_validation)
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
    lines = [line.strip() for line in Path(args.file).read_text(encoding='utf-8').splitlines()]
    return run_commands([line for line in lines if line and not line.startswith('#')])


def parse_crop(text):
    values = [int(value) for value in text.split(',')]
    if len(values) != 4:
        raise argparse.ArgumentTypeError('expected X,Y,W,H')
    return values


def command_shot(args):
    with running_session() as session:
        name = next_shot_name(session.instance, args.target)
        original = capture(session, args.target, name)
    view = original.with_name(f'{name}-view.png')
    size, scale = make_view(original, args.target, view, args.crop, args.max_width, args.exposure)
    print(f'original: {original}')
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
    command.add_argument('--model', help='glTF path relative to MiniEngine/ModelViewer, passed as -model. Uses the orbit camera')
    command.add_argument('--no-build', action='store_true')
    command.add_argument('--gpu-validation', action='store_true',
                         help='pass -gpu_debug 1 for GPU-based validation, which checks on the GPU the state and descriptor of each '
                              'resource a shader accesses. Without it, the state of a resource a shader reads is not checked. '
                              'Debug only. Slower')

    command = add_command('do', 'send commands to the running instance one by one and stop at the first error. '
                                'Reports a crash or a hang but leaves a hung app to stop. "do help" lists the commands', command_do)
    command.add_argument('commands', nargs='+', metavar='command')

    command = add_command('run', 'send the commands in a file, one per line. # starts a comment', command_run)
    command.add_argument('file')

    command = add_command('shot', 'capture a target and write a small PNG to look at', command_shot)
    command.add_argument('target', choices=['scene', 'overlay', 'display'])
    command.add_argument('--crop', type=parse_crop, metavar='X,Y,W,H', help='in original pixels')
    command.add_argument('--max-width', type=int, default=PREVIEW_MAX_WIDTH)
    command.add_argument('--exposure', type=float, default=PREVIEW_EXPOSURE, help='for the scene view only')

    command = add_command('stop', 'quit the running instance, killing it if it hangs, and summarize its log with the D3D12 messages. '
                                  'Exits with 1 unless the app ended normally, whatever the messages', command_stop)
    command.add_argument('--kill', action='store_true', help='kill without sending quit')

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
