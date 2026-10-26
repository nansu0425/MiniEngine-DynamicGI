"""확인 script 들이 같이 쓰는 PASS/FAIL 판정과 app 종료의 판정.

script 마다 PASS/FAIL 의 형식과 종료 때의 판정이 달라지지 않도록 여기에 모은다.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer import LIVE_DEVICE_ID, describe_d3d12, stop_instance


class Checks:
    def __init__(self):
        self.failures = []

    def __call__(self, name, passed, detail=''):
        print(f'{"PASS" if passed else "FAIL"}  {name}' + (f': {detail}' if detail != '' else ''), flush=True)
        if not passed:
            self.failures.append(name)
        return passed


def stop_and_check(instance, check):
    """app 을 끝내고 정상 종료와 D3D12 message 를 판정한다. stop_instance 의 결과를 돌려준다."""
    stopped = stop_instance(instance)
    check('quit ends the app normally', stopped['result'] == 'quit' and stopped['exit_reason'] == 'normal',
          f'{stopped["result"]}, exit reason={stopped["exit_reason"]}')
    d3d12 = stopped['d3d12']
    lines = '; '.join(describe_d3d12(d3d12))
    running = [message for message in d3d12['running'].values() if message['severity'] in ('CORRUPTION', 'ERROR', 'WARNING')]
    check('no D3D12 corruption, error or warning while running', d3d12['logged'] and not running, lines)
    at_exit = {id: message['count'] for id, message in (d3d12['at_exit'] or {}).items()}
    check(f'the D3D12 exit report has only one #{LIVE_DEVICE_ID} LIVE_DEVICE', at_exit == {LIVE_DEVICE_ID: 1}, lines)
    return stopped
