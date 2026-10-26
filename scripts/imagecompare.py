"""float image 두 장의 차와, 같은 image 의 두 추정이 같은 기대값을 갖는지를 수치로 낸다.

PT 의 확인 script 와, 나중에 PT 와 다른 기능의 결과를 견주는 script 가 같은 식을 쓰도록 여기에 모은다.

  python scripts/imagecompare.py diff <x.dds> <reference.dds>
  python scripts/imagecompare.py agree <a0.dds> <a1.dds> <b0.dds> <b1.dds>
  python scripts/imagecompare.py selftest

합은 모두 rgb 전체에 대한 것이고 alpha 는 보지 않는다.

agree 의 r 은 두 설정 A 와 B 가 같은 기대값을 갖는지 본다. A0, A1 과 B0, B1 은 각각 Seed 만 다른 독립 추정이다.

  r = RMSE(A0, B1) / sqrt((RMSE(A0, A1)^2 + RMSE(B0, B1)^2) / 2)

pixel 의 noise 분산을 σA^2, σB^2 라 하면 분자의 제곱은 σA^2 + σB^2 에 B 와 A 의 기대값 차 b 의 제곱 평균을 더한 것이고, 분모의 각 항은
같은 설정 두 추정의 차라 그 분산의 두 배다. 그래서 r^2 ~ 1 + b^2 / σ^2 이고 σ^2 = σA^2 + σB^2 는 A 와 B 의 한 추정끼리의 차가 갖는 noise 다.
pixel 마다 부호가 달라 image 평균에서는 지워지는 bias 도 b^2 로 더해진다. 분자를 A0 와 B1 로 짓는 것은 Seed 가 같은 A0 와 B0 가 앞의 반사에서
같은 난수를 써 서로 닮기 때문이다. 정규화하지 않은 RMSE 를 쓰는 것은, rRMSE 의 분모가 각 image 의 noise 를 담아 분산이 큰 쪽의 비율을
작게 만들기 때문이다. 비율에서는 정규화가 지워진다.
"""

import argparse
import sys
from collections import namedtuple
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from imagefiles import ImageFileError, read_dds_rgba32f

# bias 와 noise 는 r^2 = 1 + bias^2 / noise^2 를 만족하는 rms 다.
Agreement = namedtuple('Agreement', 'ratio bias noise')

SELFTEST_SHAPE = (1080, 1920, 3)
SELFTEST_NOISE = (0.1, 0.2)
SELFTEST_BIAS_TO_NOISE = 0.3
# SELFTEST_SHAPE 의 독립 Gaussian noise 에서 r 이 흔들리는 폭은 1e-3 보다 작다 (어림). seed 를 고정하므로 결과는 실행마다 같다.
SELFTEST_TOLERANCE = 3e-3
# bias 는 r 의 분자와 분모의 차의 제곱근이라 r 보다 흔들린다. bias / noise 로 견준다.
SELFTEST_BIAS_TOLERANCE = 1e-2


def rgb_of(image):
    return np.asarray(image, dtype=np.float64)[..., :3]


def rmse(x, y):
    return float(np.sqrt(np.mean(np.square(rgb_of(x) - rgb_of(y)))))


def relative_rmse(x, reference):
    reference = rgb_of(reference)
    return float(np.sqrt(np.sum(np.square(rgb_of(x) - reference)) / np.sum(np.square(reference))))


def agreement(a0, a1, b0, b1):
    across = rmse(a0, b1) ** 2
    noise = (rmse(a0, a1) ** 2 + rmse(b0, b1) ** 2) / 2.0
    return Agreement(float(np.sqrt(across / noise)), float(np.sqrt(max(0.0, across - noise))), float(np.sqrt(noise)))


def read_images(paths):
    return [read_dds_rgba32f(path) for path in paths]


def command_diff(args):
    x, reference = read_images([args.x, args.reference])
    differing = int(np.count_nonzero(np.any(rgb_of(x) != rgb_of(reference), axis=2)))
    print(f'rRMSE {relative_rmse(x, reference):.6g}, RMSE {rmse(x, reference):.6g}, '
          f'max abs diff {float(np.max(np.abs(rgb_of(x) - rgb_of(reference)))):.6g}, {differing} pixels differ')
    return 0


def command_agree(args):
    result = agreement(*read_images([args.a0, args.a1, args.b0, args.b1]))
    print(f'r {result.ratio:.6g}, bias rms {result.bias:.6g}, noise rms {result.noise:.6g}')
    return 0


def command_selftest(args):
    """r 의 식이 틀리면 bias 가 있어도 확인 script 의 기준을 통과할 수 있으므로, 답을 아는 합성 image 로 확인한다."""
    rng = np.random.default_rng(0)
    signal = rng.uniform(0.0, 1.0, SELFTEST_SHAPE).astype(np.float32)

    def estimate(noise, bias=0.0):
        return (signal + bias + rng.normal(0.0, noise, SELFTEST_SHAPE)).astype(np.float32)

    failures = []

    def check(name, passed, detail):
        print(f'{"PASS" if passed else "FAIL"}  {name}: {detail}')
        if not passed:
            failures.append(name)

    check('the same image has RMSE 0 and rRMSE 0', rmse(signal, signal) == 0.0 and relative_rmse(signal, signal) == 0.0,
          f'RMSE {rmse(signal, signal)}, rRMSE {relative_rmse(signal, signal)}')

    noise_a, noise_b = SELFTEST_NOISE
    unbiased = agreement(estimate(noise_a), estimate(noise_a), estimate(noise_b), estimate(noise_b))
    check(f'r of unbiased estimates with noise {noise_a} and {noise_b} is within {SELFTEST_TOLERANCE} of 1',
          abs(unbiased.ratio - 1.0) <= SELFTEST_TOLERANCE, f'r {unbiased.ratio:.6g}')

    # 부호가 pixel 마다 바뀌는 bias 라 image 평균은 거의 같다.
    noise = np.hypot(noise_a, noise_b)
    sign = np.where((np.indices(SELFTEST_SHAPE[:2]).sum(axis=0) % 2 == 0)[..., np.newaxis], 1.0, -1.0)
    bias = SELFTEST_BIAS_TO_NOISE * noise * sign
    a0, a1 = estimate(noise_a), estimate(noise_a)
    biased = agreement(a0, a1, estimate(noise_b, bias), estimate(noise_b, bias))
    expected = float(np.sqrt(1.0 + SELFTEST_BIAS_TO_NOISE ** 2))
    check(f'r with a bias of {SELFTEST_BIAS_TO_NOISE} noise that changes sign by pixel is {expected:.4g}',
          abs(biased.ratio - expected) <= SELFTEST_TOLERANCE and abs(biased.bias / noise - SELFTEST_BIAS_TO_NOISE) <= SELFTEST_BIAS_TOLERANCE,
          f'r {biased.ratio:.6g}, bias {biased.bias:.6g} for {SELFTEST_BIAS_TO_NOISE * noise:.6g}, noise {biased.noise:.6g} for {noise:.6g}')

    print(f'{len(failures)} failed' if failures else 'all passed')
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('diff', help='rRMSE and RMSE of x against reference')
    command.add_argument('x')
    command.add_argument('reference')
    command.set_defaults(handler=command_diff)
    command = commands.add_parser('agree', help='r, bias and noise of two settings A and B from two seeds each')
    for name in ('a0', 'a1', 'b0', 'b1'):
        command.add_argument(name)
    command.set_defaults(handler=command_agree)
    command = commands.add_parser('selftest', help='check r and RMSE on synthetic images whose answers are known')
    command.set_defaults(handler=command_selftest)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except ImageFileError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
