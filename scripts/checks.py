"""확인 script 들이 같이 쓰는 PASS/FAIL 판정, 측정 scene 마다의 판정, app 종료의 판정.

script 마다 PASS/FAIL 의 형식과 종료 때의 판정이 달라지지 않도록 여기에 모은다.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer import LIVE_DEVICE_ID, describe_d3d12, measurement_scenes, stop_instance


class Checks:
    def __init__(self):
        self.failures = []

    def __call__(self, name, passed, detail=''):
        print(f'{"PASS" if passed else "FAIL"}  {name}' + (f': {detail}' if detail != '' else ''), flush=True)
        if not passed:
            self.failures.append(name)
        return passed

    def premised(self, name, premise, passed, detail=''):
        """판정이 뜻을 가지려면 화면에 있어야 하는 것 (premise) 이 있으면 passed 로 판정하고, 없으면 N/A (해당 없음) 를 출력한다.
        N/A 는 실패로 세지 않는다. premise 를 돌려준다."""
        if premise:
            self(name, passed, detail)
        else:
            print(f'N/A   {name}' + (f': {detail}' if detail != '' else ''), flush=True)
        return premise


def check_in_measurement_scenes(session, check, judge):
    """measurement_scenes() 의 scene 을 차례로 열고 judge(session, check, scene 이름) 로 판정한다. judge 는 판정의 이름마다 연
    scene 에서 판정했는지 (N/A 가 아닌지) 를 돌려준다. 어느 scene 에서도 판정하지 못한 판정은 실패다."""
    scenes = measurement_scenes()
    judged = {}
    for scene_name, scene in scenes.items():
        session.ask_ok(f'scene open {scene}')
        for name, premise in judge(session, check, scene_name).items():
            judged.setdefault(name, [])
            if premise:
                judged[name].append(scene_name)
    for name, judged_scenes in judged.items():
        check(f'{name} is judged in at least one of the {len(scenes)} measurement scenes', bool(judged_scenes),
              f'judged in {", ".join(judged_scenes) or "none"}')


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
