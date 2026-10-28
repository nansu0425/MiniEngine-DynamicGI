"""float image 두 장의 차와, 같은 image 의 두 추정이 같은 기대값을 갖는지를 수치로 낸다.

PT 의 확인 script 와, 나중에 PT 와 다른 기능의 결과를 비교하는 script 가 같은 식을 쓰도록 여기에 모은다.

  python scripts/imagecompare.py diff <x.dds> <reference.dds>
  python scripts/imagecompare.py agree <a0.dds> <a1.dds> <b0.dds> <b1.dds>
  python scripts/imagecompare.py bias <a0.dds> <a1.dds> <b0.dds> [<b1.dds>]
  python scripts/imagecompare.py selftest

합은 모두 rgb 전체에 대한 것이고 alpha 는 보지 않는다.

agree 의 r 은 두 설정 A 와 B 가 같은 기대값을 갖는지 본다. A0, A1 과 B0, B1 은 각각 Seed 만 다른 독립 추정이다.

  r = RMSE(A0, B1) / sqrt((RMSE(A0, A1)^2 + RMSE(B0, B1)^2) / 2)

pixel 의 noise 분산을 σA^2, σB^2 라 하면 분자의 제곱은 σA^2 + σB^2 에 B 와 A 의 기대값 차 b 의 제곱 평균을 더한 것이고, 분모의 각 항은
같은 설정 두 추정의 차라 그 분산의 두 배다. 그래서 r^2 ~ 1 + b^2 / σ^2 이고 σ^2 = σA^2 + σB^2 는 A 와 B 의 한 추정끼리의 차가 갖는 noise 다.
pixel 마다 부호가 달라 image 평균에서는 지워지는 bias 도 b^2 로 더해진다. 분자를 A0 와 B1 로 짓는 것은 Seed 가 같은 A0 와 B0 가 앞의 반사에서
같은 난수를 써 서로 닮기 때문이다. 정규화하지 않은 RMSE 를 쓰는 것은, rRMSE 의 분모가 각 image 의 noise 를 담아 분산이 큰 쪽의 비율을
작게 만들기 때문이다. 비율에서는 정규화가 지워진다.

r 은 기대값 차가 그 spp 의 noise 에 비해 얼마나 큰지를 나타낸다. noise 는 spp 가 늘수록 작아지므로, 같은 차도 spp 에 따라
r 이 다르다. 차가 정답 오차의 예산처럼 정해진 비 이하인지 판정할 때는 bias 로 차를 image 의 크기에 대한 비로 내고 그 상한을
둔다. 아래의 |x| 와 합은 pixel 마다 rgb 에 대한 것이고, Am, Bm 은 Seed 들의 평균이다. pixel 들은 서로 독립이라고 본다. PT 는
pixel 마다 다른 난수열을 쓴다.

  d = mean(Bm - Am 의 rgb 합) / mean(Am 의 rgb 합)            부호 있는 차. 모든 pixel 에서 한쪽으로 생기는 차를 잡는다.
  t = |A0 - B1|^2 - (|A0 - A1|^2 + |B0 - B1|^2) / 2           pixel 마다. 기대값이 그 pixel 의 |b|^2 이다.
  t = |A1 - B0|^2 - |A0 - A1|^2                               B1 이 없을 때. B0 의 noise 분산이 A 의 한 추정과 같아야 기대값이 |b|^2 이다.
  b = sqrt(mean(t) / 3) / rms(Am)                            rms 차. pixel 마다 부호가 바뀌는 차도 잡는다.

상한은 d 의 크기와 mean(t) 에 각각 표준오차 (pixel 사이의 표준편차 / sqrt(pixel 수)) 의 BOUND_STANDARD_ERRORS 배를 더한 값이다.
floor 는 차가 0 으로 나왔을 때의 상한이다. floor 가 비교할 비보다 크면 그 spp 로는 차가 그 비 아래인지 판정할 수 없다.
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
# difference 는 d, rms 는 b 이고, 둘 다 image 의 크기에 대한 비다.
Bias = namedtuple('Bias', 'difference difference_bound difference_floor rms rms_bound rms_floor')

# 정규분포에서 표준오차의 2 배는 한쪽으로 2.3% 만 넘는다.
BOUND_STANDARD_ERRORS = 2.0

SELFTEST_SHAPE = (1080, 1920, 3)
SELFTEST_NOISE = (0.1, 0.2)
SELFTEST_BIAS_TO_NOISE = 0.3
# SELFTEST_SHAPE 의 독립 Gaussian noise 에서 r 이 흔들리는 폭은 1e-3 보다 작다 (어림). seed 를 고정하므로 결과는 실행마다 같다.
SELFTEST_TOLERANCE = 3e-3
# bias 는 r 의 분자와 분모의 차의 제곱근이라 r 보다 흔들린다. bias / noise 로 비교한다.
SELFTEST_BIAS_TOLERANCE = 1e-2
# 모든 pixel 에서 같은 쪽으로 더하는 차. 작업 12 가 bias 원인 하나에 두는 예산과 같은 크기다.
SELFTEST_DIFFERENCE = 2e-3
# 상한이 표준오차를 맞게 쓰는지는 작은 image 를 여러 번 만들어, 차가 없을 때 추정이 floor 안에 드는 비율로 본다. d 는 양쪽으로 95.4%,
# b 는 한쪽으로 97.7% 가 기대값이고, SELFTEST_COVERAGE_RUNS 번의 비율이 흔들리는 폭 (표준편차 약 0.01) 의 3 배를 둔다. 표준오차를 두 배
# 틀리면 비율이 0.68 이나 0.9999 쯤이 되어 걸린다.
SELFTEST_COVERAGE_SHAPE = (64, 64, 3)
SELFTEST_COVERAGE_RUNS = 400
SELFTEST_DIFFERENCE_COVERAGE = (0.92, 0.985)
SELFTEST_RMS_COVERAGE = (0.95, 0.995)
# b 는 rms 의 비라 SELFTEST_SHAPE 에서 표준오차가 기대값의 0.5% 쯤이다 (어림).
SELFTEST_RMS_TOLERANCE = 2e-2


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


def pixels_of(image):
    return rgb_of(image).reshape(-1, 3)


def bound_margin(values):
    return BOUND_STANDARD_ERRORS * float(np.std(values, ddof=1)) / np.sqrt(values.size)


def bias(a0, a1, b0, b1=None):
    a0, a1, b0 = pixels_of(a0), pixels_of(a1), pixels_of(b0)
    a = (a0 + a1) / 2.0
    across_a = np.sum(np.square(a0 - a1), axis=1)
    if b1 is None:
        b = b0
        t = np.sum(np.square(a1 - b0), axis=1) - across_a
    else:
        b1 = pixels_of(b1)
        b = (b0 + b1) / 2.0
        t = np.sum(np.square(a0 - b1), axis=1) - (across_a + np.sum(np.square(b0 - b1), axis=1)) / 2.0

    signed = np.sum(b - a, axis=1)
    mean_a = float(np.mean(np.sum(a, axis=1)))
    difference = float(np.mean(signed)) / mean_a
    difference_floor = bound_margin(signed) / mean_a

    rms_a = float(np.sqrt(np.mean(np.square(a))))

    def relative_rms(squared_sum):
        return float(np.sqrt(max(0.0, squared_sum) / 3.0)) / rms_a

    squared, margin = float(np.mean(t)), bound_margin(t)
    return Bias(difference, abs(difference) + difference_floor, difference_floor, relative_rms(squared), relative_rms(squared + margin),
                relative_rms(margin))


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


def command_bias(args):
    result = bias(*read_images([args.a0, args.a1, args.b0] + ([args.b1] if args.b1 else [])))
    print(f'd {result.difference:.6g} (bound {result.difference_bound:.6g}, floor {result.difference_floor:.6g}), '
          f'b {result.rms:.6g} (bound {result.rms_bound:.6g}, floor {result.rms_floor:.6g})')
    return 0


def command_selftest(args):
    """r 과 bias 의 식이 틀리면 차가 있어도 확인 script 의 기준을 통과할 수 있으므로, 답을 아는 합성 image 로 확인한다."""
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
    bias_by_pixel = SELFTEST_BIAS_TO_NOISE * noise * sign
    a0, a1 = estimate(noise_a), estimate(noise_a)
    biased = agreement(a0, a1, estimate(noise_b, bias_by_pixel), estimate(noise_b, bias_by_pixel))
    expected = float(np.sqrt(1.0 + SELFTEST_BIAS_TO_NOISE ** 2))
    check(f'r with a bias of {SELFTEST_BIAS_TO_NOISE} noise that changes sign by pixel is {expected:.4g}',
          abs(biased.ratio - expected) <= SELFTEST_TOLERANCE and abs(biased.bias / noise - SELFTEST_BIAS_TO_NOISE) <= SELFTEST_BIAS_TOLERANCE,
          f'r {biased.ratio:.6g}, bias {biased.bias:.6g} for {SELFTEST_BIAS_TO_NOISE * noise:.6g}, noise {biased.noise:.6g} for {noise:.6g}')

    def describe(result):
        return (f'd {result.difference:.4g} (floor {result.difference_floor:.3g}), b {result.rms:.4g} (bound {result.rms_bound:.4g}, '
                f'floor {result.rms_floor:.3g})')

    shift = SELFTEST_DIFFERENCE * float(signal.mean())
    shifted = bias(a0, a1, estimate(noise_b, shift), estimate(noise_b, shift))
    expected_difference = shift / float(np.mean((rgb_of(a0) + rgb_of(a1)) / 2.0))
    check(f'd of a difference of {SELFTEST_DIFFERENCE:g} x the mean in every pixel is within 1.5 floors of it and below its bound',
          abs(shifted.difference - expected_difference) <= 1.5 * shifted.difference_floor and shifted.difference_bound >= expected_difference,
          f'{describe(shifted)} for d {expected_difference:.4g}')

    rms_a = float(np.sqrt(np.mean(np.square((rgb_of(a0) + rgb_of(a1)) / 2.0))))
    expected_rms = SELFTEST_BIAS_TO_NOISE * noise / rms_a
    for name, result in [('with B1', bias(a0, a1, estimate(noise_b, bias_by_pixel), estimate(noise_b, bias_by_pixel))),
                         ('without B1', bias(a0, a1, estimate(noise_a, bias_by_pixel)))]:
        check(f'{name}, b of a difference that changes sign by pixel is within {SELFTEST_RMS_TOLERANCE:g} of it relatively and d is within '
              f'1.5 floors of 0', abs(result.rms / expected_rms - 1.0) <= SELFTEST_RMS_TOLERANCE and result.rms_bound >= expected_rms and
              abs(result.difference) <= 1.5 * result.difference_floor, f'{describe(result)} for b {expected_rms:.4g}')

    small = rng.uniform(0.0, 1.0, SELFTEST_COVERAGE_SHAPE)
    covered_difference = covered_rms = 0
    for _ in range(SELFTEST_COVERAGE_RUNS):
        result = bias(*(small + rng.normal(0.0, level, SELFTEST_COVERAGE_SHAPE) for level in (noise_a, noise_a, noise_b, noise_b)))
        covered_difference += abs(result.difference) <= result.difference_floor
        covered_rms += result.rms <= result.rms_floor
    fractions = covered_difference / SELFTEST_COVERAGE_RUNS, covered_rms / SELFTEST_COVERAGE_RUNS
    check(f'without a difference, |d| is within its floor in {SELFTEST_DIFFERENCE_COVERAGE} of {SELFTEST_COVERAGE_RUNS} runs and b in '
          f'{SELFTEST_RMS_COVERAGE}',
          SELFTEST_DIFFERENCE_COVERAGE[0] <= fractions[0] <= SELFTEST_DIFFERENCE_COVERAGE[1] and
          SELFTEST_RMS_COVERAGE[0] <= fractions[1] <= SELFTEST_RMS_COVERAGE[1], f'd {fractions[0]:.3f}, b {fractions[1]:.3f}')

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
    command = commands.add_parser('bias', help='d and b of two settings A and B with their bounds, from two seeds of A and one or two of B')
    for name in ('a0', 'a1', 'b0'):
        command.add_argument(name)
    command.add_argument('b1', nargs='?')
    command.set_defaults(handler=command_bias)
    command = commands.add_parser('selftest', help='check r, bias and RMSE on synthetic images whose answers are known')
    command.set_defaults(handler=command_selftest)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except ImageFileError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
