import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer import REPO_ROOT, HarnessError


def code_version():
    """잴 때의 HEAD. commit 하지 않은 변경이 있으면 + 를 붙인다."""
    def git(*arguments):
        return subprocess.run(['git', *arguments], cwd=REPO_ROOT, capture_output=True, text=True, check=True).stdout.strip()
    return git('rev-parse', '--short', 'HEAD') + ('+' if git('status', '--porcelain') else '')


def describe_machine(session):
    """측정 기록의 config, GPU, scene 열."""
    engine = session.ask_ok('state engine')['state']['engine']
    scene = session.ask_ok('state ui')['state']['ui']['scene']
    return [engine['config'], engine['gpu'], f'{scene["width"]}x{scene["height"]}']


def append_record_row(record, section, cells):
    """record 의 section 에 있는 표의 끝에 한 줄을 추가한다."""
    lines = record.read_text(encoding='utf-8').splitlines(keepends=True)
    try:
        start = next(index for index, line in enumerate(lines) if line.rstrip() == section)
    except StopIteration:
        raise HarnessError(f'{record} has no section "{section}"')
    end = next((index for index in range(start + 1, len(lines)) if lines[index].startswith('## ')), len(lines))
    table = [index for index in range(start + 1, end) if lines[index].startswith('|')]
    if not table:
        raise HarnessError(f'the section "{section}" of {record} has no table')
    lines.insert(table[-1] + 1, '| ' + ' | '.join(cells) + ' |\n')
    record.write_text(''.join(lines), encoding='utf-8', newline='\n')
