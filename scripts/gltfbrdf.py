"""glTF 2.0 metallic-roughness BRDF 의 float64 참조 구현과, Core/Shaders/GltfBrdf.hlsli 가 읽는 coupling table 을 만드는 script.

  python scripts/gltfbrdf.py tables                    # TABLES_FILE 을 다시 만든다
  python scripts/gltfbrdf.py tables --output <path>    # 다른 곳에 만든다. 다시 만들면 byte 까지 같은지 볼 때 쓴다

BRDF 는 references/glTF 의 Specification.adoc Appendix B 의 식 (D :3697, V :3727, F :3790, 조합 :3532, :3838-3851) 에
GltfBrdf.hlsli 가 고른 세 가지 (α 의 하한, diffuse 와 specular 의 coupling, n·v ≤ 0 의 처리) 를 더한 것이고, 그 이유는 GltfBrdf.hlsli 에
있다. 여기서는 spec 의 모양 그대로 float64 로 계산한다. GltfBrdf.hlsli 는 같은 값을 float32 에서 안정한 모양으로 계산하므로, 두 구현의
차는 float32 의 반올림이어야 한다. scripts/check-gltf-brdf.py 가 이 module 을 참조로 GPU 의 값을 판정한다.

table 은 coupling 의 E_s 와 E_avg 다. E_s(μ, α) 는 f0 = DIELECTRIC_F0 인 specular lobe (α 의 하한을 걸고, height-correlated Smith, |V·H| 의
Schlick) 의 directional albedo 이고 E_avg(α) = 2∫E_s(μ, α) μ dμ 다.
  - E_s 는 μ 를 COSINE_NODES 개, α 를 ROUGHNESS_NODES 개의 node 에서 재고 그 사이를 bilinear 로 보간한다. node 는 μ 의 세제곱근과 log α 에서
    고르게 둔다. E_s 가 μ 가 작고 α 가 작은 곳에서 가파르게 바뀌기 때문이다. 이 table 을 만들 때 1e-6 ≤ μ ≤ 1 과 roughness 의 node
    사이를 포함한 격자에서 잰 보간 오차는 μ ≥ 0.03 에서 3.0e-4 (roughness 0.087, μ 0.03), 모든 μ 에서 7.3e-4 (μ 0.001) 였고, E_s 가
    보간보다 커서 energy 가 느는 쪽은 3.4e-4 까지였다. node 64 × 64 를 μ 와 roughness 에 고르게 두면 μ ≥ 0.03 에서 3.7e-3 이었다.
  - E_avg 는 E_s 의 node 값을 float32 로 반올림한 뒤 그 bilinear 보간을 μ 로 적분한 값이다. 그래서 GPU 의 coupled diffuse 를 반구에서
    적분하면 보간 오차와 무관하게 ρ (1 - E_s(μ_o)) 가 되고 (float32 의 반올림 안에서), energy 의 오차는 E_s(μ_o) 의 보간 오차만 남는다.
    roughness 사이에서 E_avg 를 E_s 와 같은 weight 로 보간하면 그 성질이 node 사이에서도 남는다.
"""

import argparse
import math
import re
import sys
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parent.parent
TABLES_FILE = REPO_ROOT / 'MiniEngine' / 'Core' / 'Shaders' / 'GltfBrdfTables.hlsli'

DIELECTRIC_F0 = 0.04
# α = roughness² 의 하한. 그 이유는 GltfBrdf.hlsli 의 kGltfMinAlpha 에 있다.
MIN_ALPHA = 1e-3
COSINE_NODES = 128
ROUGHNESS_NODES = 64
# E_s 를 잴 때 θ_h 의 구간마다, 그리고 φ 의 node 수. 두 배로 늘리면 node 값이 μ ≥ 1e-3 에서 1.7e-7, 그 아래에서 2.9e-6 까지 바뀌었다
# (α = 1, μ = 6e-5). 보간 오차 (docstring) 보다 작다.
ALBEDO_THETA_NODES = 48
ALBEDO_PHI_NODES = 64
LUMINANCE = np.array([0.2126, 0.7152, 0.0722])


# --- 식 --------------------------------------------------------------------------------------

def alpha_of(roughness):
    return np.maximum(np.square(np.clip(roughness, 0.0, 1.0)), MIN_ALPHA)


def microfacet_distribution(n_dot_h, alpha):
    """D (:3697)"""
    a2 = alpha * alpha
    return a2 * (n_dot_h > 0) / (math.pi * np.square(np.square(n_dot_h) * (a2 - 1.0) + 1.0))


def visibility(n_dot_v, n_dot_l, alpha):
    """V (:3727). χ⁺(H·L), χ⁺(H·V) 는 v 와 l 이 둘 다 위쪽이면 1 이다."""
    a2 = alpha * alpha
    return 0.5 / (n_dot_v * np.sqrt(a2 + (1.0 - a2) * np.square(n_dot_l)) + n_dot_l * np.sqrt(a2 + (1.0 - a2) * np.square(n_dot_v)))


def fresnel(f0, v_dot_h):
    """F (:3790)"""
    return f0 + (1.0 - f0) * (1.0 - np.abs(v_dot_h)) ** 5


# --- table -----------------------------------------------------------------------------------

def gauss_legendre(count):
    x, w = np.polynomial.legendre.leggauss(count)
    return (x + 1.0) / 2.0, w / 2.0


def half_angle_bounds(alpha, theta_v):
    """반사 방향 l 을 h 로 적분할 때 h 의 극각 θ_h 를 나누는 경계.

    low 보다 작은 θ_h 는 모든 방위에서 위쪽의 l 을 내고, high 보다 크면 아무 방위에서도 내지 않는다. 그 사이에서 l 이 위쪽인 방위의 범위가
    low 근처에서 빠르게 줄고, D 는 α 근처에서 빠르게 바뀌므로 둘에서 두 배씩 넓어지는 구간으로 나눈다."""
    low = math.pi / 4.0 - theta_v / 2.0
    high = math.pi / 4.0 + theta_v / 2.0
    bounds = {0.0, high}
    for start in (alpha, low):
        bound = start
        while 0.0 < bound < high:
            bounds.add(bound)
            bound *= 2.0
    return sorted(bounds)


def reflecting_azimuth(theta_h, theta_v):
    """θ_h 의 h 가 위쪽의 l 을 내는 방위 |φ_h - φ_v| 의 상한. l·n = 2(v·h)cos θ_h - cos θ_v > 0 을 φ 로 푼 것이다."""
    a = np.sin(2.0 * theta_h) * math.sin(theta_v)
    b = -math.cos(theta_v) * np.cos(2.0 * theta_h)
    safe = np.where(a > 0.0, a, 1.0)
    return np.where(a > 0.0, np.arccos(np.clip(b / safe, -1.0, 1.0)), np.where(b < 0.0, math.pi, 0.0))


def directional_albedo(cosine, alpha, reflection, theta_nodes=ALBEDO_THETA_NODES, phi_nodes=ALBEDO_PHI_NODES):
    """μ_o = cosine 에서 ∫ f cos dω_l. reflection(v·h, n·l, n·h, n·v) 는 f cos 이고 방위에 따르지 않아야 한다. l 을 h 로 바꿔
    dω_l = 4 (v·h) dω_h 로 적분하고, 방위는 대칭이라 반만 잰다. half_angle_bounds 는 specular lobe 에 맞춘 구간이다."""
    theta_v = math.acos(min(max(cosine, 0.0), 1.0))
    sin_v, cos_v = math.sin(theta_v), math.cos(theta_v)
    bounds = half_angle_bounds(alpha, theta_v)
    x, w = gauss_legendre(theta_nodes)
    theta = np.concatenate([a + (b - a) * x for a, b in zip(bounds, bounds[1:])])[:, None]
    theta_weight = np.concatenate([(b - a) * w for a, b in zip(bounds, bounds[1:])])[:, None]
    x, w = gauss_legendre(phi_nodes)
    phi_max = reflecting_azimuth(theta, theta_v)
    phi = phi_max * x[None, :]
    sin_h, cos_h = np.sin(theta), np.cos(theta)
    v_dot_h = sin_v * sin_h * np.cos(phi) + cos_v * cos_h
    n_dot_l = np.maximum(2.0 * v_dot_h * cos_h - cos_v, 0.0)
    integrand = reflection(v_dot_h, n_dot_l, cos_h, cos_v) * 4.0 * v_dot_h * sin_h
    return float(2.0 * np.sum(integrand * phi_max * w[None, :] * theta_weight))


def specular_reflection(f0, alpha):
    """f0 인 specular lobe 의 f cos 를 directional_albedo 의 reflection 으로"""
    return lambda v_dot_h, n_dot_l, cos_h, cos_v: (fresnel(f0, v_dot_h) * microfacet_distribution(cos_h, alpha)
                                                    * visibility(cos_v, n_dot_l, alpha) * n_dot_l)


def specular_albedo(cosine, alpha, f0=DIELECTRIC_F0, theta_nodes=ALBEDO_THETA_NODES, phi_nodes=ALBEDO_PHI_NODES):
    """μ_o = cosine 에서 f0 인 specular lobe 의 directional albedo"""
    return directional_albedo(cosine, alpha, specular_reflection(f0, alpha), theta_nodes, phi_nodes)


def fresnel_mix_albedo(cosine, alpha):
    """spec 의 sample implementation 의 dielectric (:3845, baseColor 1) 의 directional albedo. coupling 을 비교하려고 잰다."""
    specular = specular_reflection(DIELECTRIC_F0, alpha)
    return directional_albedo(cosine, alpha, lambda v_dot_h, n_dot_l, cos_h, cos_v: (
        (1.0 - fresnel(DIELECTRIC_F0, v_dot_h)) * n_dot_l / math.pi + specular(v_dot_h, n_dot_l, cos_h, cos_v)))


def separable_coupling_albedo(cosine, alpha):
    """diffuse 를 (1 − F(N·L))(1 − F(N·V)) 로 줄인 dielectric (:3879, baseColor 1) 의 directional albedo. coupling 을 비교하려고 잰다."""
    specular = specular_reflection(DIELECTRIC_F0, alpha)
    return directional_albedo(cosine, alpha, lambda v_dot_h, n_dot_l, cos_h, cos_v: (
        (1.0 - fresnel(DIELECTRIC_F0, n_dot_l)) * (1.0 - fresnel(DIELECTRIC_F0, cos_v)) * n_dot_l / math.pi
        + specular(v_dot_h, n_dot_l, cos_h, cos_v)))


def cosine_nodes():
    return np.linspace(0.0, 1.0, COSINE_NODES) ** 3


def alpha_nodes():
    return MIN_ALPHA ** (1.0 - np.linspace(0.0, 1.0, ROUGHNESS_NODES))


def average_of_interpolated(albedo):
    """node 값 albedo[j, i] 의 bilinear 보간을 j 마다 2∫E μ dμ 로 적분한다. μ = x³ 이라 2∫E μ dμ = 6∫E x⁵ dx 이고, 구간마다 E 가 x 의
    1 차식이라 4 점 Gauss-Legendre 가 정확하다."""
    x = np.linspace(0.0, 1.0, COSINE_NODES)
    t, w = gauss_legendre(4)
    total = np.zeros(albedo.shape[0])
    for i in range(COSINE_NODES - 1):
        points = x[i] + (x[i + 1] - x[i]) * t
        values = albedo[:, i, None] * (1.0 - t) + albedo[:, i + 1, None] * t
        total += 6.0 * np.sum(values * points ** 5 * w * (x[i + 1] - x[i]), axis=1)
    return total


def make_tables():
    albedo = np.array([[specular_albedo(cosine, alpha) for cosine in cosine_nodes()] for alpha in alpha_nodes()])
    albedo = albedo.astype(np.float32)
    return Tables(albedo, average_of_interpolated(albedo.astype(np.float64)).astype(np.float32))


def format_floats(values, per_line):
    texts = [f'{float(value):.9g}' for value in values]
    return ',\n'.join('    ' + ', '.join(texts[i:i + per_line]) for i in range(0, len(texts), per_line))


def table_source(tables):
    return (
        '// scripts/gltfbrdf.py 가 만든 파일이다. 고치지 말고 그 script 로 다시 만든다. 값의 뜻과 node 의 위치는 그 script 의 docstring 에 있다.\n'
        '\n'
        '#ifndef __GLTF_BRDF_TABLES_HLSLI__\n'
        '#define __GLTF_BRDF_TABLES_HLSLI__\n'
        '\n'
        f'static const float kGltfDielectricF0 = {DIELECTRIC_F0!r};\n'
        f'static const float kGltfMinAlpha = {MIN_ALPHA!r};\n'
        f'static const uint kSpecularAlbedoCosineNodes = {COSINE_NODES};\n'
        f'static const uint kSpecularAlbedoRoughnessNodes = {ROUGHNESS_NODES};\n'
        '\n'
        '// E_s 의 [roughness node][cosine node]\n'
        f'static const float kDielectricSpecularAlbedo[{ROUGHNESS_NODES * COSINE_NODES}] =\n{{\n'
        f'{format_floats(tables.albedo.reshape(-1), 8)}\n}};\n'
        '\n'
        '// E_avg 의 [roughness node]\n'
        f'static const float kAverageDielectricSpecularAlbedo[{ROUGHNESS_NODES}] =\n{{\n'
        f'{format_floats(tables.average, 8)}\n}};\n'
        '\n'
        '#endif\n')


class Tables:
    """table 의 node 값과, GltfBrdf.hlsli 의 table 조회와 같은 보간."""

    def __init__(self, albedo, average):
        self.albedo = albedo
        self.average = average

    @classmethod
    def read(cls, path=TABLES_FILE):
        text = Path(path).read_text(encoding='utf-8')

        def array(name):
            body = re.search(rf'{name}\[\d+\] =\s*\{{([^}}]*)\}}', text).group(1)
            return np.array([float(value) for value in body.replace('\n', ' ').split(',')], dtype=np.float32)

        return cls(array('kDielectricSpecularAlbedo').reshape(ROUGHNESS_NODES, COSINE_NODES), array('kAverageDielectricSpecularAlbedo'))

    @staticmethod
    def _axis(coordinate, count):
        position = np.clip(coordinate, 0.0, 1.0) * (count - 1)
        index = np.minimum(np.floor(position).astype(np.int64), count - 2)
        return index, position - index

    def _roughness_axis(self, alpha):
        return self._axis(1.0 - np.log(alpha) / math.log(MIN_ALPHA), ROUGHNESS_NODES)

    def specular_albedo(self, cosine, alpha):
        """E_s(μ, α) 의 보간"""
        j, t = self._roughness_axis(alpha)
        i, s = self._axis(np.cbrt(np.clip(cosine, 0.0, 1.0)), COSINE_NODES)
        table = self.albedo.astype(np.float64)
        low = table[j, i] * (1.0 - s) + table[j, i + 1] * s
        high = table[j + 1, i] * (1.0 - s) + table[j + 1, i + 1] * s
        return low * (1.0 - t) + high * t

    def average_specular_albedo(self, alpha):
        """E_avg(α) 의 보간"""
        j, t = self._roughness_axis(alpha)
        table = self.average.astype(np.float64)
        return table[j] * (1.0 - t) + table[j + 1] * t


# --- BRDF ------------------------------------------------------------------------------------

def dot(a, b):
    return np.sum(a * b, axis=-1)


def toward_normal(normal, view):
    """GltfBrdf.hlsli 처럼 n·v < 0 인 v 를 n 의 접평면에 대해 거울로 옮긴다."""
    return view - 2.0 * np.minimum(dot(normal, view), 0.0)[..., None] * normal


def evaluate(tables, base_color, metallic, roughness, normal, view, light):
    """f 의 rgb. 벡터는 (..., 3), metallic 과 roughness 는 (...) 배열이다.

    normal 은 정규화하고 view 와 light 는 받은 그대로 H = normalize(L + V) 에 넣는다. float32 의 단위 vector 는 길이가 1 에서 1e-7 쯤
    다르다. normal 의 길이는 방향을 바꾸지 않지만 spec 의 (N·H)²(α² - 1) + 1 에서 1 - (N·H)² 를 그만큼 바꿔 specular peak 의 D 를
    크게 바꾸므로 지운다. view 와 light 를 정규화하면 L + V 의 방향이 바뀌는데, grazing 의 peak 에서는 그 변화가 α 와 견줄 만하므로
    (μ_o = 1e-4 에서 약 5e-4 rad, 계산) 그 값은 받은 입력의 BRDF 가 아니다."""
    base_color, normal, view, light = (np.asarray(value, dtype=np.float64) for value in (base_color, normal, view, light))
    metallic, roughness = np.asarray(metallic, dtype=np.float64), np.asarray(roughness, dtype=np.float64)
    normal = normal / np.linalg.norm(normal, axis=-1, keepdims=True)
    view = toward_normal(normal, view)
    alpha = alpha_of(roughness)
    n_dot_v = np.clip(dot(normal, view), 0.0, 1.0)
    n_dot_l = dot(normal, light)
    lit = n_dot_l > 0.0
    n_dot_l = np.where(lit, n_dot_l, 1.0)
    half = view + light
    half /= np.maximum(np.linalg.norm(half, axis=-1), 1e-300)[..., None]
    f0 = DIELECTRIC_F0 * (1.0 - metallic)[..., None] + base_color * metallic[..., None]
    specular = (fresnel(f0, dot(view, half)[..., None])
                * (microfacet_distribution(dot(normal, half), alpha) * visibility(n_dot_v, n_dot_l, alpha))[..., None])
    coupling = ((1.0 - tables.specular_albedo(n_dot_v, alpha)) * (1.0 - tables.specular_albedo(n_dot_l, alpha))
                / (math.pi * (1.0 - tables.average_specular_albedo(alpha))))
    diffuse = base_color * ((1.0 - metallic) * coupling)[..., None]
    return np.where(lit[..., None], specular + diffuse, 0.0)


# --- 명령 ------------------------------------------------------------------------------------

def command_tables(args):
    output = Path(args.output) if args.output else TABLES_FILE
    # .gitattributes 가 이 파일을 CRLF 로 checkout 하므로 같은 줄 끝으로 써서, 다시 만든 파일이 checkout 한 파일과 byte 까지 같게 한다.
    output.write_bytes(table_source(make_tables()).replace('\n', '\r\n').encode('utf-8'))
    print(f'wrote {output}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('tables', help='write the coupling tables GltfBrdf.hlsli reads')
    command.add_argument('--output', help=f'where to write instead of {TABLES_FILE.relative_to(REPO_ROOT)}')
    command.set_defaults(handler=command_tables)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
