"""glTF BRDF (Core/Shaders/GltfBrdf.hlsli) 가 spec 의 식을 따르고 positive, reciprocal, energy conserving 이며, path tracer 와 raster 가 그
BRDF 를 쓰는지 판정한다.

  python scripts/check-gltf-brdf.py check   # 아래 기준을 판정한다

번호는 작업 8 의 완료 기준의 번호다. app 을 Debug 로 test scene 에서 한 번, Sponza 에서 한 번 띄우고 끝내며, 이미 띄운 harness instance 는
먼저 끝낸다. 하나라도 실패하면 exit 1 이다. 실행마다 끝낼 때 실행 중의 D3D12 CORRUPTION, ERROR, WARNING 이 0 이고 종료 때 LIVE_DEVICE
하나만 남는지도 본다. PT 의 값은 capture pt 의 float export 로 본다. 이 script 밖에서 보는 기준:
  [R3] raster 의 변경 전후: viewer.py snapshot compare task-8 이 exit 0 이고, CHANGED 인 shot 은 sheet 와 report 로 본다. [R2] 는 이
       script 가 그 compare 의 shot 으로 판정하므로 compare 를 먼저 돌린다.
  [R5] raster 의 나머지: check-raster-lighting.py check 와, check-directional-light.py check 의 [P5] 와 [R1]
  [M1] 의 panel: Path Tracer panel 의 Material 을 사람이나 computer use 로 바꾸고 그 전후의 state 로 본다.
  그대로인 것: pathtracerchecks.BASE 의 Material 은 Lambert 이고, check-lambert-path.py check, check-path-tracer-export.py check,
       check-directional-light.py check 가 통과한다. 그래서 작업 5, 6 의 측정 기록이 Lambert 조건으로 유효하다.
  Debug, Profile, Release build, viewer.py selftest, 문서

BRDF 단위 test: path tracer 와 raster 가 include 하는 바로 그 HLSL 함수 (MakeGltfBrdfInput, EvaluateGltfBrdf, SampleGltfBrdfDirection 과 그
안의 table 조회) 를 app 의 brdf 명령으로 GPU 에서 돌리고 (MiniEngine/Model/PathTracer/BrdfTest.cpp), 결과를 Python 이 판정한다. 참조는
scripts/gltfbrdf.py 의 float64 구현이고, 같은 module 이 table 을 만든다. 격자의 cell 은 roughness CELL_ROUGHNESS 와 table 의 roughness
node 사이마다의 가운데, metallic CELL_METALLIC, μ_o CELL_COSINES 의 곱이다. roughness 0.03 은 α 의 하한 1e-3 아래라 roughness 0 과 같은
cell 이고, 하한이 걸리지 않는 작은 roughness 는 node 사이의 값이 덮는다. μ_o < 0 은 normal map 때문에 dot(n_s, v) < 0 인 경우다. energy
는 baseColor 1 로 잰다. cell 마다 world 의 normal, tangent 와 v 의 방위를 무작위로 고른다.
  Q 는 cell 의 ∫ f max(0, n·l) dω_l 과 ∫ f max(0, n·l) g(l) dω_l 을 GPU 의 eval 값으로 반구에서 적분한 것이다 (brdf quadrature,
  g(l) = (1 + t·l)², t 는 cell 의 tangent). θ_h 의 구간마다와 φ 에 QUADRATURE_NODES 의 node 로 잰 Q 와, θ_h 와 φ 의 node 를 각각 두 배로
  늘려 잰 Q 의 차가 그 기준 허용 오차의 1/4 이하여야 판정이 유효하고, 판정에는 두 배로 잰 값을 쓴다. IS 는 같은 두 적분을 GPU 의 sample
  함수로 잰 평균이다 (brdf sample). cell 마다 MIN_SAMPLES 이상을 고르고, 두 평균의 4σ 가 IS_PRECISION 이하가 되게 MAX_SAMPLES 까지
  늘린다 (σ 는 평균의 standard error). 해상도 조건을 못 맞추면 FAIL 이다.
  [B1] 식: 무작위 (material, v, l) PAIRS 쌍 (v, l 은 n 의 위쪽), specular peak 주변의 PEAK_PAIRS 쌍 (h 를 n 에서 PEAK_SPREAD α 안에서 고른
       l = reflect(-v, h)), 경계 BOUNDARY_PAIRS 쌍 (μ_i ≤ 0, μ_o ≤ 0, roughness 0) 에서 channel 마다
       |f_GPU - f_ref| ≤ 1e-3 f_ref + 1e-7 이다. material 은 색 있는 baseColor 와 [0, 1] 의 metallic, roughness 이고 0 과 1 을 섞는다.
       peak 쌍의 반은 μ_o < 0.05 의 grazing 이다. f_ref 는 GPU 에 준 float32 입력을 float64 로 올려 normal 만 정규화하고 v 와 l 은 받은
       그대로 spec 의 식에 넣은 값이다 (그 이유는 gltfbrdf.evaluate). path tracer 의 직접광이 이 f 를 그대로 쓴다.
  [B2] 양수와 유한 (spec :3651): [B1] 의 eval 값, 모든 cell 의 Q 가 평가한 값과 IS 가 고른 방향의 f 가 0 이상이고 유한하다. sample 이 낸
       방향은 단위 vector (||l| - 1| ≤ 1e-5) 이고 weight 는 0 이상이며 유한하다.
  [B3] 상호성 (spec :3651): [B1] 의 무작위 쌍 (μ_i > 0, μ_o > 0) 에서 channel 마다 |f(v, l) - f(l, v)| ≤ 1e-5 max(f(v, l), f(l, v)) + 1e-9.
  [B4] sampling 과 eval 의 일치: roughness ≥ 0.1 인 모든 cell 에서 두 적분 모두 |IS - Q| ≤ 4σ + 1e-3 이다. g 가 방향에 따라 바뀌므로
       방향이나 frame 변환이 틀리면 weight 의 합이 맞아도 걸린다.
  [B5] energy (spec :3651): 모든 cell 에서 A(μ_o) = ∫ f max(0, n·l) dω_l ≤ 1 + 1e-3 이고, metallic 0 이고 μ_o > 0 인 cell 에서
       |A - 1| ≤ 1e-3 이다. A 는 roughness ≥ 0.1 이면 Q, 그 아래는 IS 이고, IS 로 잰 cell 은 허용 오차에 4σ 를 더한다. metallic > 0 인
       cell 의 A < 1 은 metal 의 single scattering 손실이라 판정하지 않고 값만 출력하고 KEPT_DIR 의 CELLS_FILE 에 남긴다.
       1e-3 의 근거: albedo 0.5 인 장면에서 반사마다의 오차 e 는 image 에 약 e / (1 - 0.5) 로 쌓이므로 (추론) 2e-3 이고, 작업 12 가 잡을
       정답 오차 1% 의 1/5 이다.

test scene: +z 를 보는 flat quad 를 roughness QUAD_ROUGHNESS x metallic QUAD_METALLIC 의 격자로 두고 baseColor 는 QUAD_COLORS 에서
돌려 쓴다 (texture 없는 material factor). light 는 direction LIGHT_DIRECTION, intensity float 의 π 다. quad 의 pixel 은 Front Face view 의
앞면을 가장자리 EDGE_PIXELS 깎고 column 과 row 로 나눈 것이다. PT 는 Material glTF, Max Bounces 1, Sky Radiance 0, Russian roulette off,
16 spp 다. sky 와 emission 이 0 이라 path 의 값은 처음 맞은 곳의 직접광뿐이다.
  [S3] NEE 의 절대값: Geometric Normals on 의 PT export 가 quad 마다 3 x 3 pick 점에서 참조의 f(v, l) E max(0, n_g·l) 와 channel 마다
       상대 S3_TOLERANCE 안이다. v 는 pick 의 위치에서 state camera 의 위치로 가는 방향, n_g 는 pick 의 normal 이다.
  [R1] raster 와 PT: harness baseline (TAA, motion blur, adaptive exposure off) 의 raster scene capture q 와 Geometric Normals off 의 PT
       export v 가 깎은 앞면 pixel 모두에서 |q - v| ≤ 2^-m max(v, 2^-14) + 1e-3 v 다 (m 은 R11G11B10 의 mantissa bit 수
       R11G11B10_MANTISSA). shadow map 이 원인인 차이는 shadow map 을 고치지 않고 근거를 보고한다.

Sponza: harness baseline 의 시점과 light 에서 Material glTF 로 쌓는다. glTF 의 furnace 는 baseColor 1, metallic 0, material 의 roughness
인 BRDF 에 Furnace Albedo 를 곱하므로 모든 view 방향에서 directional albedo 가 Furnace Albedo 다 (PathTracerCS.hlsl).
  [S1] 여러 번 반사하는 emission furnace (EMISSION_FURNACE_GLTF: Albedo 0.875, Emission 0.125, Sky 1, Geometric Normals on, Max Bounces
       256, FURNACE_SAMPLES spp): 해는 어디서나 Emission / (1 - Albedo) = 1 이다. Russian roulette off 와 on 각각에서 Seed 0, 1 의 export
       에 대해, Seed 0 을 A0 로 둔 r 과 Seed 1 을 A0 로 둔 r 이 둘 다 r = RMSE(A0, 1) / sqrt(RMSE(A0, A1)^2 / 2) ≤ AGREEMENT_LIMIT (상수 1
       을 B 로 준 imagecompare 의 r) 이고 NaN 이나 Inf 로 버린 sample 이 0 이다. RMSE(A0, 1) 이 EXACT_RMSE 이하면 r 과 무관하게 통과다.
       0.875 와 0.125 는 float 에서 정확하고 0.875^256 = 1.5e-15 라 Max Bounces 의 잘림은 무시할 수 있다.
       처음에는 Albedo 1 의 white furnace (WHITE_FURNACE) 로 판정했다. glTF 의 반사마다의 weight 는 1 근처에서 흔들리고 albedo 1 의 path
       는 수천 번 반사해 throughput 의 꼬리가 무거워서, 같은 code 에서 Seed 의 순서만 바꿔도 r 이 0.66 ~ 1.25 로 흔들렸다 (가장 큰 pixel
       784). 그래서 white furnace 는 판정하지 않고 두 순서의 r 과 평균을 출력하고 export 를 남긴다.
  [S2] 한 번 반사하는 furnace (ONE_REFLECTION_FURNACE: Albedo 1, Emission 1, Sky 1, Max Bounces 1, Geometric Normals on), Seed 0 과 1,
       ONE_REFLECTION_POSES 의 시점마다: miss pixel 은 정확히 1 이고 courtyard-up 에는 miss pixel 이 있다 (baseline 시점에는 없다). hit
       pixel 의 기대값은 1 + A = 2 이고, hit pixel 에서 상수 2 대비 r ≤ AGREEMENT_LIMIT 이며 두 Seed 를 평균한 pixel 값의 평균 m 이
       |m - 2| ≤ ONE_REFLECTION_TOLERANCE + 4 SE 다 (SE 는 그 pixel 값의 표준편차 / sqrt(pixel 수)). invalid sample 0. hit 과 miss 는 같은
       Seed 의 camera ray 로 Max Bounces 0, Emission 0 을 쌓아 정확히 0 인 pixel 과 1 인 pixel 이다.
  [S4] 유한성과 visibility: viewer.SNAPSHOT_POSES 의 네 시점과 directlightchecks.SPONZA_LIGHTS 의 두 방향에서 SPONZA_SCENE (Geometric
       Normals off, Max Bounces 64, Russian roulette on, 256 spp) 을 쌓아 invalid sample 이 0 이다. 그리고 check-directional-light.py 의
       [P6], [P7] 을 Material glTF 로 통과한다. [P6] 의 보이는데 어두움은 glTF 의 f 가 0 이 아닌 pixel (Metallic 이 1 이고 Base Color 가
       0 인 곳 밖) 에서 세고 임계값은 그대로다.
  [S5] Lambert 의 bit 재현: BEFORE_DIR/start-pt.dds (47f6794, 조건은 BEFORE_DIR/manifest.json: baseline 시점, light 기본 방향, intensity
       4, Sky 1, Max Bounces 64, Russian roulette on, Seed 0, Geometric Normals off, 64 spp) 와 Material Lambert 로 같은 조건에서 쌓은
       export 의 rgba 가 bit 까지 같다.
  [R4] 기본 화면의 밝기: scripts/harness-baseline/editor.ini 의 Exposure 주석의 규칙대로, baseline 시점에서 Exposure 2^EXPOSURE_STOPS 의
       display 와 adaptive exposure 를 ADAPTATION_FRAMES 만큼 수렴시킨 display 의 평균 밝기를 견줘 stop 으로 가장 가까운 Exposure 가 1
       이다. 수렴은 마지막 두 밝기의 비가 ADAPTATION_SETTLED 안인 것으로 본다.
  [M1] Material 설정: 기본은 glTF 다. Material key 가 없는 editor.ini (harness baseline) 로 띄우면 state 와 settings get Path Tracer 가
       glTF 이고, settings set 으로 바꾸면 state 가 따르고 pt 의 resets 가 1 늘며, 끝낼 때 editor.ini 에 마지막 값이 남는다.
       check-lambert-path.py 의 [8], [9] 도 Material 을 넣어 판정한다.

raster 의 변경 전후:
  [R2] SNAPSHOT_DIR/task-8 (47f6794) 의 baseline shot 네 개의 scene 과 가장 새 compare 의 같은 shot 에서, 값이 0 인지 아닌지 (rgb 가 모두
       0 이면 0) 가 달라진 pixel 은 before 와 after 가 모두 2^-14 미만이다. 빛이 닿는 곳은 그대로이고 밝기만 바뀐다.

source 와 생성 파일:
  [C1] BRDF_FUNCTIONS 가 BRDF_FILE 에만 정의되어 있고, PathTracerCS, DefaultPS (와 그것을 include 하는 variant 셋), BRDF test shader 가
       CALLED_BY 의 함수를 부른다. Model/Renderer 에 FORBIDDEN_RASTER_NAMES 가 없다. PathTracerCS 는 MakeGltfBrdfInput 을 한 번 불러
       vertex 의 brdf 를 만들고 그것을 직접광과 반사에 넘긴다 (code 를 읽어 본 것을 grep 의 수로 확인한다).
  [C2] scripts/gltfbrdf.py tables 를 다시 돌리면 repo 에 둔 Core/Shaders/GltfBrdfTables.hlsli 와 byte 까지 같다.

GPU 의 수치는 Debug 에서 잰다. shader 는 모든 config 에서 같은 optimization 으로 compile 되고 (MiniEngine/PropertySheets/Build.props 의
FxCompile, Debug 는 debug info 만 더한다) table 은 shader 의 상수라서, Profile 과 Release 에서도 같다고 본다 (추론). raster 의 수치는
harness baseline 에서만 보고, TAA, motion blur, adaptive exposure 를 켠 화면은 [R3] 의 app-default 로만 본다.

발표 자료는 KEPT_DIR 에 남긴다: brdf/albedo.svg (roughness 별 directional albedo. glTF 의 dielectric 과 metal 은 GPU 의 cell, 비교하는
fresnel_mix 와 (1 - F)(1 - F) coupling 은 Python 참조), [S1] 의 emission furnace 와 white furnace, [S2] 와 Sponza 의 export 와 보기용 PNG,
test-scene/ 의 [R1] raster 와 PT 쌍.
"""

import argparse
import configparser
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltfbrdf
from checks import Checks, stop_and_check
from directlightchecks import (EDGE_PIXELS, LIGHT_SECTION, SPONZA_LIGHTS, WAIT_TIMEOUT, accumulate_export, check_bounces, check_sponza_direct,
                               overlay, set_light)
from gltfwriter import GLTF_FLOAT, GLTF_UNSIGNED_SHORT, BufferBuilder, flatten
from imagecompare import agreement, rmse
from imagefiles import ImageFileError, read_dds_rgba32f
from imagemasks import column_groups, eroded
from pathtracerchecks import BASE, FURNACE_SAMPLES, SECTION, WHITE_FURNACE, path_tracer_state, set_path_tracer
from viewer import (BASELINE_DIR, HARNESS_DIR, PREVIEW_EXPOSURE, REPO_ROOT, SETTLE_FRAMES, SNAPSHOT_DIR, SNAPSHOT_POSES, VIEWER_DIR, HarnessError,
                    Session, baseline_pose, capture, make_view, start_instance, stop_instance)

SCENE_DIR = HARNESS_DIR / 'gltf-brdf'
SCENE_NAME = 'GltfBrdf'
KEPT_DIR = HARNESS_DIR / 'kept' / 'gltf-brdf'
CELLS_FILE = KEPT_DIR / 'brdf' / 'cells.json'
BEFORE_DIR = KEPT_DIR / 'before'
SNAPSHOT_NAME = 'task-8'
GLTF = {'Material': 'glTF'}

SEED = 8
PAIRS = 1 << 16
PEAK_PAIRS = 1 << 14
BOUNDARY_PAIRS = 1 << 14
PEAK_SPREAD = 3.0
FORMULA_RELATIVE = 1e-3
FORMULA_ABSOLUTE = 1e-7
RECIPROCITY_RELATIVE = 1e-5
RECIPROCITY_ABSOLUTE = 1e-9
UNIT_TOLERANCE = 1e-5

CELL_ROUGHNESS = [0.0, 0.03, 0.1, 0.2, 0.3, 0.5, 0.7, 1.0]
CELL_METALLIC = [0.0, 0.5, 1.0]
CELL_COSINES = [1.0, 0.7, 0.5, 0.3, 0.1, 0.03, -0.1]
QUADRATURE_ROUGHNESS = 0.1
QUADRATURE_NODES = (64, 128)
ENERGY_TOLERANCE = 1e-3
AGREEMENT_TOLERANCE = 1e-3
IS_PRECISION = 1e-3
# brdf sample 의 기록 하나가 고르는 방향 수 (256 thread x SAMPLES_PER_THREAD)
SAMPLES_PER_THREAD = 256
CHUNK_SAMPLES = 256 * SAMPLES_PER_THREAD
MIN_SAMPLES = 1 << 20
MAX_SAMPLES = 1 << 27
# brdf 명령 하나가 harness 의 hang 판정 (frame 이 멈춘 10 s) 보다 짧게 끝나도록 나누는 기록 수
SAMPLE_RECORDS_PER_COMMAND = 2048
QUADRATURE_RECORDS_PER_COMMAND = 512
COMMAND_TIMEOUT = 300

QUAD_ROUGHNESS = [0.2, 0.4, 0.6, 0.8, 1.0]
QUAD_METALLIC = [0.0, 0.5, 1.0]
QUAD_COLORS = [(0.9, 0.5, 0.2), (0.2, 0.6, 0.9), (0.5, 0.9, 0.3), (0.9, 0.3, 0.7), (0.7, 0.7, 0.2)]
QUAD_SPACING = 2.4
# light 는 위 앞쪽에서 온다. 앞면이 +z 인 quad 와의 cos 는 0.6 이고, camera 쪽의 specular peak 에서 θ_h 가 약 27° 떨어져 pixel 안에서
# f 가 거의 바뀌지 않는다.
LIGHT_DIRECTION = (0.0, 0.8, 0.6)
INTENSITY = float(np.float32(math.pi))
FRONT_FACE_COLOR = (0, 255, 0)
MIN_QUAD_PIXELS = 100
TEST_DIRECT = BASE | GLTF | {'Sky Radiance': 0, 'Max Bounces': 1, 'Russian Roulette': 0, 'Target Samples': 16}
S3_TOLERANCE = 1e-3
R11G11B10_MANTISSA = (6, 6, 5)

AGREEMENT_LIMIT = 1.01
EXACT_RMSE = 1e-6
ONE_REFLECTION_FURNACE = BASE | GLTF | {'Furnace': 1, 'Furnace Albedo': 1, 'Furnace Emission': 1, 'Sky Radiance': 1, 'Max Bounces': 1,
                                        'Geometric Normals': 1, 'Russian Roulette': 0, 'Target Samples': FURNACE_SAMPLES}
ONE_REFLECTION_TOLERANCE = 1e-3
# courtyard-up 에는 sky 가 보여 miss pixel 이 있다.
ONE_REFLECTION_POSES = ['start', 'courtyard-up']
EMISSION_FURNACE_GLTF = BASE | GLTF | {'Furnace': 1, 'Furnace Albedo': 0.875, 'Furnace Emission': 0.125, 'Sky Radiance': 1, 'Max Bounces': 256,
                                       'Geometric Normals': 1, 'Target Samples': FURNACE_SAMPLES}
SPONZA_SCENE = BASE | GLTF | {'Geometric Normals': 0, 'Max Bounces': 64, 'Russian Roulette': 1, 'Target Samples': 256}
EXPOSURE_STOPS = [-2, -1, 0, 1, 2]
# adaptive exposure 를 켜고 기다리는 frame 수. 둘째 뒤의 밝기가 첫째 뒤와 ADAPTATION_SETTLED 안이면 수렴했다고 본다.
ADAPTATION_FRAMES = [600, 300]
ADAPTATION_SETTLED = 0.01

BRDF_FILE = 'MiniEngine/Core/Shaders/GltfBrdf.hlsli'
BRDF_FUNCTIONS = ['MakeGltfBrdfInput', 'EvaluateGltfBrdf', 'SampleGltfBrdfDirection', 'GetDielectricSpecularAlbedo',
                  'GetAverageDielectricSpecularAlbedo']
CALLED_FUNCTIONS = ['MakeGltfBrdfInput', 'EvaluateGltfBrdf', 'SampleGltfBrdfDirection']
PATH_TRACER_SHADER = 'MiniEngine/Model/PathTracer/PathTracerCS.hlsl'
RASTER_SHADER = 'MiniEngine/Model/Renderer/DefaultPS.hlsl'
TEST_SHADER = 'MiniEngine/Model/PathTracer/BrdfTestCS.hlsl'
# raster 는 directional light 만 평가하고 방향을 고르지 않는다.
CALLED_BY = {PATH_TRACER_SHADER: CALLED_FUNCTIONS, RASTER_SHADER: CALLED_FUNCTIONS[:2], TEST_SHADER: CALLED_FUNCTIONS}
FORBIDDEN_RASTER_NAMES = ['Diffuse_Burley', 'G_Shlick_Smith_Hable', 'Specular_D_GGX', 'kDielectricSpecular', 'c_diff']
GRAPH_COSINES = [1.0, 0.5, 0.3, 0.1, 0.03]

TABLES = gltfbrdf.Tables.read()


def test_scene_materials():
    """test scene 의 quad 마다 (roughness, metallic, baseColor). glTF 의 material 순서이고, quad 는 roughness 가 column, metallic 이
    row 다."""
    return [(roughness, metallic, QUAD_COLORS[(column + row) % len(QUAD_COLORS)])
            for row, metallic in enumerate(QUAD_METALLIC) for column, roughness in enumerate(QUAD_ROUGHNESS)]


def write_test_scene():
    """+z 를 보는 flat quad 를 QUAD_ROUGHNESS x QUAD_METALLIC 의 격자로 둔다. -model 의 OrbitCamera 는 model 중심의 +z 쪽에서 -z 를 본다."""
    builder = BufferBuilder()
    position = builder.add(flatten([(-1.0, 1.0, 0.0), (1.0, 1.0, 0.0), (1.0, -1.0, 0.0), (-1.0, -1.0, 0.0)]), GLTF_FLOAT, 'VEC3', 4, with_bounds=True)
    normal = builder.add(flatten([(0.0, 0.0, 1.0)] * 4), GLTF_FLOAT, 'VEC3', 4)
    uv = builder.add(flatten([(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]), GLTF_FLOAT, 'VEC2', 4)
    # glTF 의 앞면은 counter-clockwise 다. +z 에서 보면 두 triangle 모두 counter-clockwise 다.
    indices = builder.add([0, 3, 2, 0, 2, 1], GLTF_UNSIGNED_SHORT, 'SCALAR', 6)
    materials, meshes, nodes = [], [], []
    for index, (roughness, metallic, color) in enumerate(test_scene_materials()):
        row, column = divmod(index, len(QUAD_ROUGHNESS))
        materials.append({'name': f'roughness {roughness:g} metallic {metallic:g}', 'pbrMetallicRoughness': {
            'baseColorFactor': [*color, 1.0], 'metallicFactor': metallic, 'roughnessFactor': roughness}})
        meshes.append({'name': f'quad {index}', 'primitives': [{'attributes': {'POSITION': position, 'NORMAL': normal, 'TEXCOORD_0': uv},
                                                             'indices': indices, 'material': index}]})
        nodes.append({'name': f'quad {index}', 'mesh': index,
                      'translation': [(column - (len(QUAD_ROUGHNESS) - 1) / 2) * QUAD_SPACING, ((len(QUAD_METALLIC) - 1) / 2 - row) * QUAD_SPACING, 0.0]})
    gltf = {
        'asset': {'version': '2.0', 'generator': 'scripts/check-gltf-brdf.py'},
        'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}],
        'nodes': nodes,
        'meshes': meshes,
        'materials': materials,
        'buffers': [{'uri': f'{SCENE_NAME}.bin', 'byteLength': len(builder.data)}],
        'bufferViews': builder.views,
        'accessors': builder.accessors,
    }
    (SCENE_DIR / f'{SCENE_NAME}.bin').write_bytes(builder.data)
    (SCENE_DIR / f'{SCENE_NAME}.gltf').write_text(json.dumps(gltf, indent=2), encoding='utf-8')


# --- 입력 --------------------------------------------------------------------------------------

def unit(vectors):
    return vectors / np.linalg.norm(vectors, axis=-1, keepdims=True)


def random_frames(rng, count):
    """무작위 normal 과 그에 수직인 두 단위 vector"""
    normal = unit(rng.normal(size=(count, 3)))
    tangent = unit(np.cross(normal, rng.normal(size=(count, 3))))
    return normal, tangent, np.cross(normal, tangent)


def directions(normal, tangent, bitangent, cosine, azimuth):
    sine = np.sqrt(np.maximum(0.0, 1.0 - cosine ** 2))
    return (cosine[:, None] * normal + (sine * np.cos(azimuth))[:, None] * tangent + (sine * np.sin(azimuth))[:, None] * bitangent)


def random_materials(rng, count):
    base_color = rng.uniform(0.0, 1.0, (count, 3))
    metallic = rng.choice([0.0, 1.0, -1.0], count, p=[0.25, 0.25, 0.5])
    metallic = np.where(metallic < 0.0, rng.uniform(0.0, 1.0, count), metallic)
    roughness = rng.choice([0.0, 1.0, -1.0], count, p=[0.1, 0.1, 0.8])
    roughness = np.where(roughness < 0.0, rng.uniform(0.0, 1.0, count), roughness)
    return base_color, metallic, roughness


def records(base_color, metallic, normal, roughness, third, third_w, fourth, fourth_w):
    """brdf 명령의 64 byte 기록. float32 로 반올림한 값이 GPU 와 참조의 입력이다."""
    data = np.zeros((len(metallic), 16), dtype=np.float32)
    data[:, 0:3], data[:, 3] = base_color, metallic
    data[:, 4:7], data[:, 7] = normal, roughness
    data[:, 8:11], data[:, 12:15] = third, fourth
    data[:, 11], data[:, 15] = third_w, fourth_w
    return data


def evaluation_pairs(rng):
    """[B1] 의 무작위, peak, 경계 쌍. 이름마다 기록을 돌려준다. 무작위 쌍 뒤에는 v 와 l 을 바꾼 쌍이 붙는다 ([B3])."""
    sets = {}
    normal, tangent, bitangent = random_frames(rng, PAIRS)
    view = directions(normal, tangent, bitangent, rng.uniform(0.0, 1.0, PAIRS), rng.uniform(0.0, 2 * math.pi, PAIRS))
    light = directions(normal, tangent, bitangent, rng.uniform(0.0, 1.0, PAIRS), rng.uniform(0.0, 2 * math.pi, PAIRS))
    material = random_materials(rng, PAIRS)
    sets['random'] = records(material[0], material[1], normal, material[2], view, 0, light, 0)
    sets['swapped'] = records(material[0], material[1], normal, material[2], light, 0, view, 0)

    normal, tangent, bitangent = random_frames(rng, PEAK_PAIRS)
    base_color, metallic, roughness = random_materials(rng, PEAK_PAIRS)
    roughness = np.where(rng.uniform(size=PEAK_PAIRS) < 0.5, rng.uniform(0.0, 0.1, PEAK_PAIRS), roughness)
    alpha = gltfbrdf.alpha_of(roughness.astype(np.float32).astype(np.float64))
    view_cosine = np.concatenate([rng.uniform(0.0, 1.0, PEAK_PAIRS // 2), rng.uniform(0.0, 0.05, PEAK_PAIRS - PEAK_PAIRS // 2)])
    view = directions(normal, tangent, bitangent, view_cosine, rng.uniform(0.0, 2 * math.pi, PEAK_PAIRS))
    half = directions(normal, tangent, bitangent, np.cos(np.arctan(PEAK_SPREAD * alpha * rng.uniform(size=PEAK_PAIRS))),
                      rng.uniform(0.0, 2 * math.pi, PEAK_PAIRS))
    light = unit(2.0 * np.sum(view * half, axis=1)[:, None] * half - view)
    above = np.sum(normal * light, axis=1) > 0.0
    sets['peak'] = records(base_color, metallic, normal, roughness, view, 0, light, 0)[above]

    count = BOUNDARY_PAIRS // 4
    parts = []
    for kind in ['light below', 'view below', 'roughness 0', 'on the horizon']:
        normal, tangent, bitangent = random_frames(rng, count)
        base_color, metallic, roughness = random_materials(rng, count)
        view_cosine, light_cosine = rng.uniform(0.0, 1.0, count), rng.uniform(0.0, 1.0, count)
        if kind == 'light below':
            light_cosine = -light_cosine
        elif kind == 'view below':
            view_cosine = -view_cosine
        elif kind == 'roughness 0':
            roughness = np.zeros(count)
        else:
            # normal 이 축이라 float32 에서도 μ 가 정확히 0 이다.
            normal = np.tile([0.0, 0.0, 1.0], (count, 1))
            tangent, bitangent = np.tile([1.0, 0.0, 0.0], (count, 1)), np.tile([0.0, 1.0, 0.0], (count, 1))
            zero = rng.uniform(size=count) < 0.5
            view_cosine = np.where(zero, 0.0, view_cosine)
            light_cosine = np.where(zero, light_cosine, 0.0)
        view = directions(normal, tangent, bitangent, view_cosine, rng.uniform(0.0, 2 * math.pi, count))
        light = directions(normal, tangent, bitangent, light_cosine, rng.uniform(0.0, 2 * math.pi, count))
        parts.append(records(base_color, metallic, normal, roughness, view, 0, light, 0))
    sets['boundary'] = np.concatenate(parts)
    return sets


def reference_of(data):
    data = data.astype(np.float64)
    return gltfbrdf.evaluate(TABLES, data[:, 0:3], data[:, 3], data[:, 7], data[:, 4:7], data[:, 8:11], data[:, 12:15])


def roughness_cells():
    """CELL_ROUGHNESS 와 table 의 roughness node 사이마다의 가운데"""
    x = np.linspace(0.0, 1.0, gltfbrdf.ROUGHNESS_NODES)
    middles = np.sqrt(gltfbrdf.MIN_ALPHA ** (1.0 - (x[:-1] + x[1:]) / 2.0))
    return sorted(set(CELL_ROUGHNESS) | set(float(value) for value in middles))


def make_cells(rng):
    cells = [(roughness, metallic, cosine) for roughness in roughness_cells() for metallic in CELL_METALLIC for cosine in CELL_COSINES]
    count = len(cells)
    roughness, metallic, cosine = (np.array(column) for column in zip(*cells))
    normal, tangent, bitangent = random_frames(rng, count)
    view = directions(normal, tangent, bitangent, cosine, rng.uniform(0.0, 2 * math.pi, count))
    data = records(np.ones((count, 3)), metallic, normal, roughness, tangent, 0, view, 0)
    return cells, data


# --- GPU ---------------------------------------------------------------------------------------

class Gpu:
    """brdf 명령으로 기록을 보내고 결과를 읽는다."""

    def __init__(self, session):
        self.session = session
        self.folder = Path(session.instance['run_dir']) / 'brdf'
        self.folder.mkdir(exist_ok=True)
        self.count = 0

    def run(self, mode, parameters, data, result_floats):
        self.count += 1
        input_path, output_path = self.folder / f'{self.count:04d}-{mode}.in', self.folder / f'{self.count:04d}-{mode}.out'
        input_path.write_bytes(np.ascontiguousarray(data, dtype=np.float32).tobytes())
        self.session.ask_ok(f'brdf {mode} {parameters} {input_path} {output_path}'.replace('  ', ' '), timeout=COMMAND_TIMEOUT)
        return np.frombuffer(output_path.read_bytes(), dtype=np.float32).reshape(len(data), result_floats)

    def evaluate(self, data):
        return self.run('evaluate', '', data, 4)[:, :3]

    def quadrature(self, data, nodes):
        parts = [self.run('quadrature', f'{nodes[0]}x{nodes[1]}', data[i:i + QUADRATURE_RECORDS_PER_COMMAND], 8)
                 for i in range(0, len(data), QUADRATURE_RECORDS_PER_COMMAND)]
        return np.concatenate(parts).astype(np.float64)

    def sample(self, data):
        parts = [self.run('sample', str(SAMPLES_PER_THREAD), data[i:i + SAMPLE_RECORDS_PER_COMMAND], 16)
                 for i in range(0, len(data), SAMPLE_RECORDS_PER_COMMAND)]
        return np.concatenate(parts).astype(np.float64)


class Sampling:
    """cell 마다 IS 의 합. 4 sigma 가 IS_PRECISION 이하가 될 때까지 chunk 를 더한다."""

    def __init__(self, cells):
        self.cells = cells
        self.chunks = np.zeros(len(cells), dtype=np.int64)
        self.sums = np.zeros((len(cells), 16))

    def add(self, gpu, wanted):
        """cell 마다 wanted 개가 되도록 chunk 를 더 고른다."""
        cell, chunk = [], []
        for index, total in enumerate(wanted):
            for value in range(self.chunks[index], total):
                cell.append(index)
                chunk.append(value)
        if not cell:
            return
        data = self.cells[cell].copy()
        data[:, 11] = np.array(cell, dtype=np.uint32).view(np.float32)
        data[:, 15] = np.array(chunk, dtype=np.uint32).view(np.float32)
        results = gpu.sample(data)
        np.add.at(self.sums, np.array(cell), np.where(np.arange(16) == 11, 0.0, results))
        largest = np.zeros(len(self.cells))
        np.maximum.at(largest, np.array(cell), results[:, 11])
        self.sums[:, 11] = np.maximum(self.sums[:, 11], largest)
        self.chunks = np.maximum(self.chunks, wanted)

    def samples(self):
        return self.chunks * CHUNK_SAMPLES

    def mean_and_error(self, first):
        """first 가 0 이면 ∫ f cos, 8 이면 ∫ f cos g 의 (평균, 4 sigma) rgb"""
        n = self.samples()[:, None].astype(np.float64)
        mean = self.sums[:, first:first + 3] / n
        variance = np.maximum(self.sums[:, first + 4:first + 7] / n - mean ** 2, 0.0)
        return mean, 4.0 * np.sqrt(variance / n)


def sample_until_precise(gpu, cell_data):
    sampling = Sampling(cell_data)
    sampling.add(gpu, np.full(len(cell_data), MIN_SAMPLES // CHUNK_SAMPLES))
    while True:
        worst = np.maximum(sampling.mean_and_error(0)[1].max(axis=1), sampling.mean_and_error(8)[1].max(axis=1))
        # 4 sigma 는 sample 수의 제곱근에 반비례하므로 IS_PRECISION 에 닿는 수를 어림해 조금 넉넉히 고른다.
        needed = np.ceil(sampling.chunks * (worst / IS_PRECISION) ** 2 * 1.2).astype(np.int64)
        wanted = np.where(worst > IS_PRECISION, np.minimum(np.maximum(needed, sampling.chunks + 1), MAX_SAMPLES // CHUNK_SAMPLES), sampling.chunks)
        if np.array_equal(wanted, sampling.chunks):
            return sampling, worst
        sampling.add(gpu, wanted)


# --- 판정 --------------------------------------------------------------------------------------

def check_formula(gpu, check, rng):
    pairs = evaluation_pairs(rng)
    names = list(pairs)
    values = dict(zip(names, np.split(gpu.evaluate(np.concatenate([pairs[name] for name in names])),
                                       np.cumsum([len(pairs[name]) for name in names])[:-1])))
    details, passed = [], True
    for name in ['random', 'peak', 'boundary']:
        reference = reference_of(pairs[name])
        error = np.abs(values[name].astype(np.float64) - reference)
        allowed = FORMULA_RELATIVE * reference + FORMULA_ABSOLUTE
        ratio = error / allowed
        worst = np.unravel_index(np.argmax(ratio), ratio.shape)
        record = pairs[name][worst[0]]
        failing = int(np.count_nonzero(np.any(error > allowed, axis=1)))
        passed = passed and failing == 0
        details.append(f'{name} {len(reference)} pairs, {failing} off; largest error / allowed {ratio[worst]:.3g} '
                       f'(f {values[name][worst]:.7g} vs {reference[worst]:.7g}, roughness {record[7]:.4g}, metallic {record[3]:.3g}, '
                       f'mu_o {float(np.dot(record[4:7], record[8:11])):.4g}, mu_i {float(np.dot(record[4:7], record[12:15])):.4g})')
    check(f'[B1] the GPU f is within {FORMULA_RELATIVE:g} f_ref + {FORMULA_ABSOLUTE:g} of the float64 reference on random, peak and boundary pairs',
          passed, '; '.join(details))

    every = np.concatenate([values[name] for name in names])
    invalid = int(np.count_nonzero(~np.all(np.isfinite(every) & (every >= 0.0), axis=1)))

    forward, backward = values['random'].astype(np.float64), values['swapped'].astype(np.float64)
    allowed = RECIPROCITY_RELATIVE * np.maximum(forward, backward) + RECIPROCITY_ABSOLUTE
    ratio = np.abs(forward - backward) / allowed
    check(f'[B3] f(v, l) and f(l, v) agree within {RECIPROCITY_RELATIVE:g} of the larger + {RECIPROCITY_ABSOLUTE:g} on {PAIRS} pairs',
          bool(np.all(ratio <= 1.0)), f'{int(np.count_nonzero(np.any(ratio > 1.0, axis=1)))} pairs off, largest difference / allowed {ratio.max():.3g}')
    return invalid, len(every)


def check_cells(gpu, check, rng, invalid_pairs):
    cells, data = make_cells(rng)
    coarse = gpu.quadrature(data, QUADRATURE_NODES)
    fine = gpu.quadrature(data, (2 * QUADRATURE_NODES[0], 2 * QUADRATURE_NODES[1]))
    sampling, worst_precision = sample_until_precise(gpu, data)
    samples = sampling.samples()

    invalid_evaluations = int(coarse[:, 3].sum() + fine[:, 3].sum())
    evaluations = int(coarse[:, 7].sum() + fine[:, 7].sum())
    sums = sampling.sums
    invalid_values, invalid_weights = (int(sums[:, column].sum()) for column in (3, 7))
    largest_length_error = float(sums[:, 11].max())
    check('[B2] every f evaluated is finite and not negative, every sampled direction is a unit vector and every weight is finite and not negative',
          invalid_pairs[0] + invalid_evaluations + invalid_values + invalid_weights == 0 and largest_length_error <= UNIT_TOLERANCE,
          f'invalid f {invalid_pairs[0]} of {invalid_pairs[1]} pairs, {invalid_evaluations} of {evaluations} quadrature nodes, {invalid_values} of '
          f'{int(samples.sum())} samples; largest |length - 1| of the sampled directions {largest_length_error:.2e}; '
          f'{invalid_weights} invalid weights')

    roughness = np.array([cell[0] for cell in cells])
    metallic = np.array([cell[1] for cell in cells])
    cosine = np.array([cell[2] for cell in cells])
    by_quadrature = roughness >= QUADRATURE_ROUGHNESS
    precise = (worst_precision <= IS_PRECISION) & (samples >= MIN_SAMPLES)

    results = []
    agreement_off, resolution_off = [], []
    for first, name in [(0, 'f cos'), (4, 'f cos g')]:
        q_coarse, q_fine = coarse[:, first:first + 3], fine[:, first:first + 3]
        sampled, error = sampling.mean_and_error(2 * first)
        allowed = error + AGREEMENT_TOLERANCE
        off = by_quadrature & np.any(np.abs(sampled - q_fine) > allowed, axis=1)
        unresolved = by_quadrature & (np.any(np.abs(q_coarse - q_fine) > allowed / 4.0, axis=1) | ~precise)
        agreement_off += [f'{name} roughness {roughness[i]:.4g} metallic {metallic[i]:g} mu_o {cosine[i]:g}: IS {sampled[i, 0]:.6f} Q {q_fine[i, 0]:.6f} '
                          f'4 sigma {error[i, 0]:.1e}' for i in np.nonzero(off)[0]]
        resolution_off += [f'{name} roughness {roughness[i]:.4g} metallic {metallic[i]:g} mu_o {cosine[i]:g}: Q {q_coarse[i, 0]:.6f} -> {q_fine[i, 0]:.6f}, '
                           f'4 sigma {error[i].max():.1e} with {samples[i]} samples' for i in np.nonzero(unresolved)[0]]
        results.append((q_coarse, q_fine, sampled, error))
    def largest(values):
        return float(np.max(values[by_quadrature]))

    summary = [f'{name}: largest |IS - Q| {largest(np.abs(r[2] - r[1])):.2e}, largest |IS - Q| - 4 sigma {largest(np.abs(r[2] - r[1]) - r[3]):.2e}, '
               f'largest |Q - Q coarse| {largest(np.abs(r[1] - r[0])):.2e}, largest 4 sigma {largest(r[3]):.2e}'
               for (name, r) in zip(['f cos', 'f cos g'], results)]
    check(f'[B4] IS and Q agree within 4 sigma + {AGREEMENT_TOLERANCE:g} for f cos and f cos g in all {int(by_quadrature.sum())} cells of roughness '
          f'>= {QUADRATURE_ROUGHNESS:g}, at the resolution the criterion needs', not agreement_off and not resolution_off,
          '; '.join(summary + [f'samples per cell {samples.min()} ~ {samples.max()}'] + agreement_off[:5] + resolution_off[:5]))

    q_coarse, q_fine, sampled, error = results[0]
    albedo = np.where(by_quadrature[:, None], q_fine, sampled)
    margin = np.where(by_quadrature[:, None], 0.0, error)
    unresolved = np.where(by_quadrature, np.any(np.abs(q_coarse - q_fine) > ENERGY_TOLERANCE / 4.0, axis=1), ~precise)
    over = np.any(albedo > 1.0 + ENERGY_TOLERANCE + margin, axis=1)
    preserving = (metallic == 0.0) & (cosine > 0.0)
    off_one = preserving & np.any(np.abs(albedo - 1.0) > ENERGY_TOLERANCE + margin, axis=1)
    deviation = np.abs(albedo - 1.0).max(axis=1)
    by_sampling = ~by_quadrature
    check(f'[B5] A <= 1 + {ENERGY_TOLERANCE:g} in all {len(cells)} cells and |A - 1| <= {ENERGY_TOLERANCE:g} in the {int(preserving.sum())} '
          f'dielectric cells with mu_o > 0, at the resolution the criterion needs', not over.any() and not off_one.any() and not unresolved.any(),
          f'largest A {albedo.max():.6f} (by Q {albedo[by_quadrature].max():.6f}, by IS {albedo[by_sampling].max():.6f} with 4 sigma up to '
          f'{error[by_sampling].max():.1e}); dielectric largest |A - 1| by Q {deviation[preserving & by_quadrature].max():.2e}, '
          f'by IS {deviation[preserving & by_sampling].max():.2e} (minus 4 sigma {(np.abs(albedo - 1.0) - margin)[preserving & by_sampling].max():.2e}); '
          f'largest |Q - Q coarse| {float(np.abs(q_coarse - q_fine)[by_quadrature].max()):.2e}; '
          f'{int(over.sum())} over, {int(off_one.sum())} off 1, {int(unresolved.sum())} unresolved')
    for value in CELL_METALLIC[1:]:
        for cos_value in CELL_COSINES:
            chosen = (metallic == value) & (cosine == cos_value) & np.isin(roughness, CELL_ROUGHNESS)
            print(f'      metallic {value:g} mu_o {cos_value:g}: A by roughness ' +
                  ', '.join(f'{roughness[i]:g} {albedo[i, 0]:.4f}' for i in np.nonzero(chosen)[0]))

    CELLS_FILE.parent.mkdir(parents=True, exist_ok=True)
    CELLS_FILE.write_text(json.dumps([{
        'roughness': float(roughness[i]), 'metallic': float(metallic[i]), 'view cosine': float(cosine[i]),
        'A': float(albedo[i, 0]), 'A by': 'Q' if by_quadrature[i] else 'IS',
        'Q': [float(results[0][1][i, 0]), float(results[1][1][i, 0])], 'Q coarse': [float(results[0][0][i, 0]), float(results[1][0][i, 0])],
        'IS': [float(results[0][2][i, 0]), float(results[1][2][i, 0])], 'IS 4 sigma': [float(results[0][3][i, 0]), float(results[1][3][i, 0])],
        'samples': int(samples[i])} for i in range(len(cells))], indent=1), encoding='utf-8')


def check_tables(check):
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder) / gltfbrdf.TABLES_FILE.name
        subprocess.run([sys.executable, str(Path(__file__).resolve().parent / 'gltfbrdf.py'), 'tables', '--output', str(output)],
                       check=True, capture_output=True)
        same = output.read_bytes() == gltfbrdf.TABLES_FILE.read_bytes()
    check(f'[C2] scripts/gltfbrdf.py tables writes {gltfbrdf.TABLES_FILE.relative_to(REPO_ROOT).as_posix()} byte for byte', same)


def keep(path, target, folder=KEPT_DIR):
    folder.mkdir(parents=True, exist_ok=True)
    kept = folder / path.name
    shutil.copy2(path, kept)
    make_view(kept, target, kept.with_suffix('.png'), None, 1 << 16, PREVIEW_EXPOSURE)


def vector_of(fields):
    return np.array([float(fields[axis]) for axis in 'xyz'])


def export(session, settings, name, keep_it=False):
    """settings 로 처음부터 쌓은 PT export 의 rgba 와 NaN 이나 Inf 라서 버린 sample 수"""
    rgba, invalid, path = accumulate_export(session, settings, name)
    if keep_it:
        keep(path, 'pt')
    return rgba, invalid


# --- test scene: [S3], [R1] ----------------------------------------------------------------

def quad_masks(front_face):
    """Front Face view 의 앞면을 EDGE_PIXELS 깎아 quad 마다 나눈 mask. column 이 왼쪽부터이고 그 안에서 row 가 위부터다."""
    front = eroded(np.all(front_face == FRONT_FACE_COLOR, axis=2), EDGE_PIXELS)
    return [row.T for column in column_groups(front) for row in column_groups(column.T)]


def pick_points(mask):
    """mask 의 bounding box 를 4 등분한 3 x 3 점. quad 는 화면에서 직사각형이라 모두 mask 안이다."""
    ys, xs = np.nonzero(mask)
    return [(int(round(np.quantile(xs, qx))), int(round(np.quantile(ys, qy)))) for qy in (0.25, 0.5, 0.75) for qx in (0.25, 0.5, 0.75)]


def relative_to_r11g11b10(raster, path_tracer):
    """|q - v| 를 [R1] 의 허용 오차 2^-m max(v, 2^-14) + 1e-3 v 로 나눈 값"""
    q, v = raster.astype(np.float64), path_tracer.astype(np.float64)
    return np.abs(q - v) / (2.0 ** -np.array(R11G11B10_MANTISSA) * np.maximum(v, 2.0 ** -14) + 1e-3 * v)


def check_test_scene(session, check):
    direction = ','.join(f'{value:g}' for value in LIGHT_DIRECTION)
    session.ask_ok(f'light set direction={direction} color=1,1,1 intensity={INTENSITY!r} position=0,0,0')
    light = vector_of(session.ask_ok('state light')['state']['light']['direction'])
    camera = vector_of(session.ask_ok('state camera')['state']['camera'])
    scene = session.ask_ok('state ui')['state']['ui']['scene']

    set_path_tracer(session, {'Show': 0})
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    raster_path = capture(session, 'scene', 'raster')
    raster = read_dds_rgba32f(raster_path)[..., :3]
    masks = quad_masks(overlay(session, 'Debug: Front Face', 'front-face'))
    sizes = [int(mask.sum()) for mask in masks]
    materials = test_scene_materials()
    if not check(f'the test scene shows {len(materials)} quads of at least {MIN_QUAD_PIXELS} pixels after trimming {EDGE_PIXELS} edge pixels',
                 len(masks) == len(materials) and min(sizes, default=0) >= MIN_QUAD_PIXELS, f'{len(masks)} quads of {sizes} pixels'):
        return
    geometric, invalid_geometric, geometric_path = accumulate_export(session, TEST_DIRECT | {'Geometric Normals': 1}, 'pt-geometric')
    shading, invalid_shading, shading_path = accumulate_export(session, TEST_DIRECT, 'pt-shading')
    set_path_tracer(session, {'Show': 0})

    off, worst, picked = [], (0.0, ''), 0
    for mask in masks:
        for x, y in pick_points(mask):
            pick = session.ask_ok(f'pick {scene["left"] + x} {scene["top"] + y}')['pick']
            roughness, metallic, color = materials[int(pick['material'])]
            position, normal = vector_of(pick['position']), vector_of(pick['normal'])
            view = (camera - position) / np.linalg.norm(camera - position)
            f = gltfbrdf.evaluate(TABLES, np.array(color, dtype=np.float32), np.float32(metallic), np.float32(roughness), normal, view, light)
            expected = f * INTENSITY * max(0.0, float(normal @ light))
            error = np.abs(geometric[y, x, :3] - expected) / expected
            picked += 1
            detail = f'roughness {roughness:g} metallic {metallic:g} at ({x}, {y}): {geometric[y, x, :3]} vs {expected}'
            if error.max() > worst[0]:
                worst = (float(error.max()), detail)
            if error.max() > S3_TOLERANCE:
                off.append(detail)
    check(f'[S3] the path tracer direct light is f E cos of the float64 reference within {S3_TOLERANCE:g} at {picked} picked points',
          not off and invalid_geometric == 0, f'largest relative error {worst[0]:.2e} ({worst[1]}); {len(off)} off; '
          f'{invalid_geometric} invalid samples' + ('; ' + '; '.join(off[:3]) if off else ''))

    ratios = [relative_to_r11g11b10(raster[mask], shading[mask][:, :3]) for mask in masks]
    every = np.concatenate(ratios)
    check('[R1] the raster and the path tracer agree within an R11G11B10 ulp + 1e-3 on every trimmed front pixel of the test scene',
          bool(np.all(every <= 1.0)) and invalid_shading == 0,
          f'{int(np.count_nonzero(np.any(every > 1.0, axis=1)))} of {len(every)} pixels off; largest difference / allowed {every.max():.3g}, '
          f'by quad {[round(float(ratio.max()), 2) for ratio in ratios]}; {invalid_shading} invalid samples')
    for path, target in [(raster_path, 'scene'), (shading_path, 'pt'), (geometric_path, 'pt')]:
        keep(path, target, KEPT_DIR / 'test-scene')


def run_test_scene(build_first, check):
    instance = start_instance('Debug', model=f'{SCENE_DIR.relative_to(VIEWER_DIR).as_posix()}/{SCENE_NAME}.gltf', build_first=build_first)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            gpu = Gpu(session)
            rng = np.random.default_rng(SEED)
            invalid_pairs = check_formula(gpu, check, rng)
            check_cells(gpu, check, rng, invalid_pairs)
            check_test_scene(session, check)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('the test scene ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)


# --- Sponza: [S1], [S2], [S4], [S5], [R4], [M1] ----------------------------------------------

def check_default_material(session, check):
    shown = session.ask_ok(f'state settings.{SECTION}')['state']['settings'][SECTION].get('Material')
    got = session.ask_ok(f'settings get {SECTION}')['values'].get('Material')
    baseline = (BASELINE_DIR / 'editor.ini').read_text(encoding='utf-8')
    check('[M1] Material is glTF in state and settings get when editor.ini has no Material key',
          shown == 'glTF' and got == 'glTF' and 'Material' not in baseline, f'state {shown}, settings get {got}')


def check_material_resets(session, check):
    set_path_tracer(session, GLTF | {'Target Samples': 1 << 20})
    session.ask_ok('pt reset')
    counts = []
    for material in ['Lambert', 'glTF']:
        session.ask_ok('pt wait samples=2', timeout=WAIT_TIMEOUT)
        before = path_tracer_state(session)['resets']
        replied = set_path_tracer(session, {'Material': material})['state']['settings'][SECTION]['Material']
        session.ask_ok('frames 2')
        counts.append((material, replied, path_tracer_state(session)['resets'] - before))
    check('[M1] settings set changes Material, state shows it and the path tracer starts over once each time',
          all(replied == material and count == 1 for material, replied, count in counts), str(counts))
    set_path_tracer(session, {'Show': 0})


def display_luminance(session, name):
    """display 의 sRGB 를 decode 한 Rec.709 luminance 의 평균 (scripts/harness-baseline/editor.ini 의 Exposure 주석)"""
    session.ask_ok(f'frames {SETTLE_FRAMES + 1}')
    srgb = np.asarray(Image.open(capture(session, 'display', name)).convert('RGB'), dtype=np.float64) / 255.0
    linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    return float(np.mean(linear @ gltfbrdf.LUMINANCE))


def check_exposure(session, check, start_pose):
    session.ask_ok(f'pose {start_pose}')
    measured = {}
    for stop in EXPOSURE_STOPS:
        session.ask_ok(f'settings set Engine Tuning: Graphics/HDR/Exposure={2.0 ** stop:g}')
        measured[stop] = display_luminance(session, f'exposure-{stop}')
    session.ask_ok('settings set Engine Tuning: Graphics/HDR/Exposure=1; Graphics/HDR/Adaptive Exposure=on')
    adapted = []
    for frames in ADAPTATION_FRAMES:
        session.ask_ok(f'frames {frames}')
        adapted.append(display_luminance(session, f'adapted-{len(adapted)}'))
    session.ask_ok('settings set Engine Tuning: Graphics/HDR/Adaptive Exposure=off')
    session.ask_ok(f'frames {SETTLE_FRAMES}')
    nearest = min(EXPOSURE_STOPS, key=lambda stop: abs(math.log2(measured[stop] / adapted[-1])))
    settled = abs(adapted[-1] / adapted[-2] - 1.0) <= ADAPTATION_SETTLED
    check('[R4] the power of two Exposure whose display is closest in stops to the adapted display is 1', nearest == 0 and settled,
          f'mean luminance by Exposure {", ".join(f"2^{stop} {value:.5f}" for stop, value in measured.items())}; adapted after '
          f'{np.cumsum(ADAPTATION_FRAMES).tolist()} frames {[round(value, 5) for value in adapted]}; '
          f'Exposure 1 is {math.log2(measured[0] / adapted[-1]):+.3f} stops from it')


def furnace_agreements(session, settings, name, keep_first):
    """Seed 0, 1 로 쌓은 export 에서 두 Seed 를 각각 A0 로 둔 (r, RMSE(A0, 1)), noise rms, 평균, 가장 큰 값, invalid sample 수"""
    images, invalid = [], 0
    for seed in (0, 1):
        rgba, bad = export(session, settings | {'Seed': seed}, f'{name}-seed{seed}', keep_it=keep_first and seed == 0)
        images.append(rgba)
        invalid += bad
    ones = np.ones_like(images[0])
    orders = [(agreement(images[first], images[1 - first], ones, ones).ratio, rmse(images[first], ones)) for first in (0, 1)]
    noise = agreement(images[0], images[1], ones, ones).noise
    means = [float(image[..., :3].mean()) for image in images]
    return orders, noise, means, float(max(image[..., :3].max() for image in images)), invalid


def check_emission_furnace(session, check):
    for russian_roulette in (0, 1):
        orders, noise, means, largest, invalid = furnace_agreements(session, EMISSION_FURNACE_GLTF | {'Russian Roulette': russian_roulette},
                                                                    f'emission-furnace-rr{russian_roulette}', keep_first=True)
        check(f'[S1] the glTF emission furnace with Russian roulette {"on" if russian_roulette else "off"} has r <= {AGREEMENT_LIMIT:g} '
              f'against 1 with either Seed as A0',
              all(error <= EXACT_RMSE or ratio <= AGREEMENT_LIMIT for ratio, error in orders) and invalid == 0,
              f'r {orders[0][0]:.5g} with Seed 0 as A0, {orders[1][0]:.5g} with Seed 1; RMSE to 1 {orders[0][1]:.3g}, {orders[1][1]:.3g}; '
              f'noise rms {noise:.3g}; means {means[0]:.6f}, {means[1]:.6f}; largest pixel {largest:.4g}; {invalid} invalid samples')
    for russian_roulette in (0, 1):
        orders, noise, means, largest, invalid = furnace_agreements(
            session, WHITE_FURNACE | GLTF | {'Russian Roulette': russian_roulette, 'Target Samples': FURNACE_SAMPLES},
            f'white-furnace-rr{russian_roulette}', keep_first=russian_roulette == 0)
        print(f'      white furnace with Russian roulette {"on" if russian_roulette else "off"}, not judged: r {orders[0][0]:.5g} with Seed 0 as '
              f'A0, {orders[1][0]:.5g} with Seed 1; means {means[0]:.6f}, {means[1]:.6f}; largest pixel {largest:.4g}; {invalid} invalid samples',
              flush=True)


def check_one_reflection_furnace(session, check, start_pose):
    for pose_name in ONE_REFLECTION_POSES:
        session.ask_ok(f'pose {SNAPSHOT_POSES[pose_name] or start_pose}')
        seen, values, invalid = [], [], 0
        for seed in (0, 1):
            # 반사하지 않고 emission 이 0 이면 pixel 의 sample 이 모두 면에 맞았을 때만 0 이고 모두 빗나갔을 때만 1 이다. 같은 Seed 는
            # 같은 camera ray 를 쏘므로 다음 run 의 pixel 도 같은 ray 로 쌓인다.
            rgba, bad = export(session, ONE_REFLECTION_FURNACE | {'Max Bounces': 0, 'Furnace Emission': 0, 'Seed': seed},
                               f'furnace-coverage-{pose_name}-seed{seed}')
            seen.append(rgba[..., :3])
            invalid += bad
            rgba, bad = export(session, ONE_REFLECTION_FURNACE | {'Seed': seed}, f'one-reflection-furnace-{pose_name}-seed{seed}', keep_it=seed == 0)
            values.append(rgba[..., :3].astype(np.float64))
            invalid += bad
        hit = np.all(seen[0] == 0.0, axis=2) & np.all(seen[1] == 0.0, axis=2)
        miss = np.all(seen[0] == 1.0, axis=2) & np.all(seen[1] == 1.0, axis=2)
        miss_exact = all(bool(np.all(value[miss] == 1.0)) for value in values)
        needs_miss = pose_name != 'start'
        expected = np.full_like(values[0][hit], 2.0)
        result = agreement(values[0][hit], values[1][hit], expected, expected)
        pixel_values = ((values[0][hit] + values[1][hit]) / 2.0).mean(axis=1)
        mean = float(pixel_values.mean())
        standard_error = float(pixel_values.std() / math.sqrt(len(pixel_values)))
        check(f'[S2] {pose_name}: the glTF one-reflection furnace is 1 on miss pixels and 1 + A = 2 on hit pixels (r <= {AGREEMENT_LIMIT:g}, '
              f'|mean - 2| <= {ONE_REFLECTION_TOLERANCE:g} + 4 SE)',
              miss_exact and (miss.any() or not needs_miss) and result.ratio <= AGREEMENT_LIMIT
              and abs(mean - 2.0) <= ONE_REFLECTION_TOLERANCE + 4.0 * standard_error and invalid == 0,
              f'{int(miss.sum())} miss pixels all 1: {miss_exact}; {int(hit.sum())} hit pixels: r {result.ratio:.5g}, mean {mean:.6f}, '
              f'SE {standard_error:.2e}; {invalid} invalid samples')


def check_sponza_finite(session, check, default_light):
    counts = {}
    for pose_name, pose in SNAPSHOT_POSES.items():
        for light_name, request in SPONZA_LIGHTS.items():
            session.ask_ok(f'pose {pose or baseline_pose()}')
            set_light(session, default_light)
            if request:
                session.ask_ok(f'light set {request}')
            rgba, invalid = export(session, SPONZA_SCENE, f'sponza-{pose_name}-{light_name}', keep_it=light_name == 'default')
            counts[f'{pose_name} {light_name}'] = (invalid, bool(np.all(np.isfinite(rgba))))
    set_light(session, default_light)
    check(f'[S4] no sample is NaN or Inf in Sponza with Material glTF, {SPONZA_SCENE["Target Samples"]} spp at the snapshot poses and lights',
          all(invalid == 0 and finite for invalid, finite in counts.values()),
          '; '.join(f'{name}: {invalid} invalid, finite {finite}' for name, (invalid, finite) in counts.items()))


def gltf_reflects_nothing(base_color, metallic):
    """glTF 의 f 가 0 인 pixel. dielectric 은 baseColor 와 무관하게 specular 가 있고 metal 은 baseColor 가 0 일 때만 0 이다."""
    return np.all(base_color == 0, axis=2) & np.all(metallic == 255, axis=2)


def check_lambert_bits(session, check):
    manifest = json.loads((BEFORE_DIR / 'manifest.json').read_text(encoding='utf-8'))
    entry = manifest['files']['start-pt.dds']
    session.ask_ok(f'pose {entry["pose"]}')
    set_light(session, entry['directional light'])
    now, _ = export(session, entry['path tracer'] | {'Material': 'Lambert'}, 'lambert-start-pt')
    before = read_dds_rgba32f(BEFORE_DIR / 'start-pt.dds')
    differing = np.any(now.view(np.uint32) != before.view(np.uint32), axis=2)
    check(f'[S5] Material Lambert accumulates the same rgba bits as before ({manifest["code"]})', not differing.any(),
          f'{int(differing.sum())} pixels differ')


def run_sponza(check):
    instance = start_instance('Debug', build_first=False)
    try:
        session = Session(instance)
        try:
            session.ask_ok('ui hide')
            camera = session.ask_ok('state camera')['state']['camera']
            start_pose = ' '.join(str(camera[key]) for key in ['x', 'y', 'z', 'heading', 'pitch'])
            default_light = session.ask_ok(f'settings get {LIGHT_SECTION}')['values']
            check_default_material(session, check)
            check_exposure(session, check, start_pose)
            check_emission_furnace(session, check)
            check_one_reflection_furnace(session, check, start_pose)
            check_sponza_finite(session, check, default_light)
            check_sponza_direct(session, check, start_pose, default_light, '[S4] [P6] glTF', GLTF, gltf_reflects_nothing,
                                lambda path: keep(path, 'pt'))
            check_bounces(session, check, start_pose, '[S4] [P7] glTF', GLTF)
            check_material_resets(session, check)
            check_lambert_bits(session, check)
        finally:
            session.close()
        stop_and_check(instance, check)
    except (HarnessError, ImageFileError) as error:
        check('Sponza ran to the end', False, str(error))
        stop_instance(instance, kill_now=True)
        return
    ini = configparser.ConfigParser(delimiters=('=',), comment_prefixes=(';',), interpolation=None)
    ini.optionxform = str
    ini.read(Path(instance['run_dir']) / 'editor.ini', encoding='utf-8')
    saved = ini[SECTION].get('Material') if ini.has_section(SECTION) else None
    check('[M1] editor.ini keeps the Material set last (Lambert)', saved == 'Lambert', f'Material={saved}')


# --- raster 의 변경 전후: [R2] ------------------------------------------------------------------

def check_lit_pixels(check):
    folder = SNAPSHOT_DIR / SNAPSHOT_NAME
    compares = sorted(folder.glob('compare-*'))
    if not compares:
        check('[R2] the baseline shots light the same pixels as before', False,
              f'no compare in {folder}. Run viewer.py snapshot compare {SNAPSHOT_NAME} first')
        return
    after_folder = compares[-1]
    details, passed = [], True
    for pose in SNAPSHOT_POSES:
        name = f'baseline-{pose}-scene.dds'
        before, after = (read_dds_rgba32f(where / name)[..., :3] for where in (folder, after_folder))
        moved = np.all(before == 0.0, axis=2) != np.all(after == 0.0, axis=2)
        faint = np.all(before < 2.0 ** -14, axis=2) & np.all(after < 2.0 ** -14, axis=2)
        lit = int(np.count_nonzero(np.any(after != 0.0, axis=2)))
        passed = passed and not np.any(moved & ~faint)
        details.append(f'{pose}: {int(moved.sum())} pixels changed between 0 and not, {int(np.count_nonzero(moved & ~faint))} of them at '
                       f'2^-14 or more; {lit} lit pixels after')
    check(f'[R2] the baseline shots light the same pixels as before ({after_folder.name})', passed, '; '.join(details))


# --- source: [C1] ------------------------------------------------------------------------------

def git_grep(*arguments):
    return subprocess.run(['git', '-c', 'core.quotepath=false', 'grep', '--untracked', '-n', *arguments], cwd=REPO_ROOT,
                          capture_output=True, text=True, encoding='utf-8').stdout.splitlines()


def check_source(check):
    names = '|'.join(BRDF_FUNCTIONS)
    definitions = [line for line in git_grep('-E', rf'^\s*[A-Za-z_][A-Za-z0-9_]*\s+({names})\s*\(', '--', '*.hlsl', '*.hlsli')
                   if not re.search(r':\s*return\s', line)]
    defined_in = sorted({line.split(':', 1)[0] for line in definitions})
    callers = {}
    for path in [PATH_TRACER_SHADER, RASTER_SHADER, TEST_SHADER]:
        text = (REPO_ROOT / path).read_text(encoding='utf-8')
        callers[path] = [name for name in CALLED_FUNCTIONS if re.search(rf'\b{name}\s*\(', text)]
    include = RASTER_SHADER.removeprefix('MiniEngine/')
    variants = sorted(line.split(':', 1)[0] for line in git_grep('-F', f'#include "{include}"', '--', '*.hlsl'))
    left = git_grep('-E', '|'.join(FORBIDDEN_RASTER_NAMES), '--', 'MiniEngine/Model/Renderer')
    path_tracer = (REPO_ROOT / PATH_TRACER_SHADER).read_text(encoding='utf-8')
    once = (len(re.findall(r'\bMakeGltfBrdfInput\s*\(', path_tracer)), len(re.findall(r'EvaluateGltfBrdf\(vertex\.brdf', path_tracer)),
            len(re.findall(r'SampleGltfBrdfDirection\(vertex\.brdf', path_tracer)))
    check('[C1] the BRDF functions are defined in one .hlsli, the path tracer, the raster and the BRDF test call them, and the old raster '
          'BRDF is gone', defined_in == [BRDF_FILE] and all(callers[path] == CALLED_BY[path] for path in callers) and len(variants) == 3
          and not left and once == (1, 1, 1),
          f'defined in {defined_in}; calls {callers}; files including DefaultPS {variants}; old names left {left}; '
          f'PathTracerCS makes the input {once[0]} time and passes it to the light {once[1]} and the reflection {once[2]} times')


# --- 발표 자료 -----------------------------------------------------------------------------------

def write_albedo_graph():
    """roughness 별 directional albedo 를 baseColor 1 로 그린 SVG. glTF 는 GPU 의 cell (CELLS_FILE), 비교하는 coupling 은 Python 이다."""
    cells = json.loads(CELLS_FILE.read_text(encoding='utf-8'))
    roughness = sorted({cell['roughness'] for cell in cells})
    panels = []
    for metallic, title in [(0.0, 'glTF dielectric, Kulla-Conty coupling (GPU)'), (1.0, 'glTF metal, single scattering (GPU)')]:
        panels.append((title, {cosine: [next(cell['A'] for cell in cells if cell['roughness'] == r and cell['metallic'] == metallic
                                              and cell['view cosine'] == cosine) for r in roughness] for cosine in GRAPH_COSINES}))
    for function, title in [(gltfbrdf.fresnel_mix_albedo, 'spec sample fresnel_mix (Python)'),
                            (gltfbrdf.separable_coupling_albedo, '(1 - F(N.L))(1 - F(N.V)) coupling (Python)')]:
        panels.append((title, {cosine: [function(cosine, float(gltfbrdf.alpha_of(r))) for r in roughness] for cosine in GRAPH_COSINES}))
    (CELLS_FILE.parent / 'albedo.json').write_text(json.dumps({'roughness': roughness, 'panels': [
        {'title': title, 'albedo': {str(cosine): values for cosine, values in curves.items()}} for title, curves in panels]}, indent=1),
        encoding='utf-8')

    width, height, margin, top, y_max = 420, 300, 50, 30, 2.0
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{2 * width}" height="{2 * height + 40}" font-family="sans-serif" font-size="12">',
             '<rect width="100%" height="100%" fill="white"/>']
    for index, (title, curves) in enumerate(panels):
        x0, y0 = (index % 2) * width + margin, (index // 2) * height + top
        w, h = width - margin - 15, height - top - margin

        def y_of(a):
            return y0 + h - min(a, y_max) / y_max * h

        parts.append(f'<text x="{x0}" y="{y0 - 10}" font-weight="bold">{title}</text>')
        parts.append(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" fill="none" stroke="#888"/>')
        for a in (0.0, 0.5, 1.0, 1.5, 2.0):
            parts.append(f'<line x1="{x0}" x2="{x0 + w}" y1="{y_of(a):.1f}" y2="{y_of(a):.1f}" stroke="#ddd"/>')
            parts.append(f'<text x="{x0 - 30}" y="{y_of(a) + 4:.1f}">{a:.1f}</text>')
        for r in (0.0, 0.5, 1.0):
            parts.append(f'<text x="{x0 + r * w - 8:.1f}" y="{y0 + h + 15}">{r:.1f}</text>')
        parts.append(f'<text x="{x0 + w / 2 - 30:.1f}" y="{y0 + h + 32}">roughness</text>')
        for color, values in zip(colors, curves.values()):
            points = ' '.join(f'{x0 + r * w:.1f},{y_of(a):.1f}' for r, a in zip(roughness, values))
            parts.append(f'<polyline fill="none" stroke="{color}" stroke-width="1.5" points="{points}"/>')
    for index, (color, cosine) in enumerate(zip(colors, GRAPH_COSINES)):
        parts.append(f'<text x="{margin + index * 110}" y="{2 * height + 30}" fill="{color}">mu_o = {cosine:g}</text>')
    parts.append('</svg>')
    (CELLS_FILE.parent / 'albedo.svg').write_text('\n'.join(parts), encoding='utf-8')


def command_check(args):
    SCENE_DIR.mkdir(parents=True, exist_ok=True)
    write_test_scene()
    check = Checks()
    run_test_scene(not args.no_build, check)
    run_sponza(check)
    check_lit_pixels(check)
    check_source(check)
    check_tables(check)
    write_albedo_graph()
    print(f'kept: {KEPT_DIR}')
    print(f'{len(check.failures)} failed' if check.failures else 'all passed')
    return 1 if check.failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    check_parser = commands.add_parser('check', help='judge the criteria above with the Debug app')
    check_parser.add_argument('--no-build', action='store_true', help='run the Debug app as it is built')
    check_parser.set_defaults(handler=command_check)
    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
