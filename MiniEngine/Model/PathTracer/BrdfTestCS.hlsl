#include "Core/Shaders/GltfBrdf.hlsli"
#include "Core/Shaders/Random.hlsli"

// BrdfTest.cpp의 Constants와 같은 layout이다. 기록과 결과의 layout은 BrdfTest.cpp가 등록하는 brdf 명령의 usage에 있다.
cbuffer Constants : register(b0)
{
    uint Mode;
    uint NumRecords;
    // 이번 dispatch의 첫 기록. 오래 걸리는 dispatch를 나눌 때 쓴다.
    uint FirstRecord;
    // quadrature에서 θ_h의 구간마다, 그리고 φ의 node 수
    uint ThetaNodes;
    uint PhiNodes;
    // sample에서 thread마다 고르는 방향 수
    uint SamplesPerThread;
};

ByteAddressBuffer Records : register(t0);
RWByteAddressBuffer Results : register(u0);

static const uint kEvaluate = 0;
static const uint kQuadrature = 1;
static const uint kSample = 2;
static const uint kGroupSize = 256;
static const uint kRecordBytes = 64;
static const float kPi = 3.14159265358979323846;
// half-angle 구간 경계의 최대 개수. α의 하한에서 두 수열이 각각 11개와 μ_o ≥ 0.03에서 7개쯤이다.
static const uint kMaxBounds = 40;

struct Material
{
    float3 baseColor;
    float metallic;
    float roughness;
    float3 normal;
};

Material LoadMaterial(uint address)
{
    const float4 first = asfloat(Records.Load4(address));
    const float4 second = asfloat(Records.Load4(address + 16));
    Material material;
    material.baseColor = first.rgb;
    material.metallic = first.a;
    material.normal = second.xyz;
    material.roughness = second.w;
    return material;
}

float3 LoadVector(uint address)
{
    return asfloat(Records.Load3(address));
}

bool IsValidValue(float3 value)
{
    return all(isfinite(value)) && all(value >= 0.0);
}

// 방향에 따라 바뀌는 시험 함수. tangent 둘레의 방위와 normal에 대한 극각 둘 다에 따라 바뀐다.
float TestWeight(float3 tangent, float3 direction)
{
    const float value = 1.0 + dot(tangent, direction);
    return value * value;
}

// --- groupshared 합 ----------------------------------------------------------------------------

static const uint kMaxSums = 16;
groupshared float gs_Sums[kGroupSize][kMaxSums];

// 모든 thread의 values를 더해 thread 0의 values에 남긴다. count개의 값만 쓴다. maxima는 더하는 대신 가장 큰 값을 남긴다.
void ReduceGroup(uint thread, inout float values[kMaxSums], uint count, uint maxima)
{
    for (uint i = 0; i < count; ++i)
        gs_Sums[thread][i] = values[i];
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = kGroupSize / 2; stride > 0; stride /= 2)
    {
        if (thread < stride)
        {
            for (uint i = 0; i < count; ++i)
            {
                const float other = gs_Sums[thread + stride][i];
                gs_Sums[thread][i] = ((maxima >> i) & 1) != 0 ? max(gs_Sums[thread][i], other) : gs_Sums[thread][i] + other;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
    for (uint i = 0; i < count; ++i)
        values[i] = gs_Sums[0][i];
}

void StoreSums(uint address, float values[kMaxSums], uint count)
{
    for (uint i = 0; i < count; ++i)
        Results.Store(address + 4 * i, asuint(values[i]));
}

// --- evaluate ----------------------------------------------------------------------------------

void Evaluate(uint record)
{
    const uint address = record * kRecordBytes;
    const Material material = LoadMaterial(address);
    const GltfBrdfInput input = MakeGltfBrdfInput(material.baseColor, material.metallic, material.roughness, material.normal, LoadVector(address + 32));
    Results.Store4(record * 16, asuint(float4(EvaluateGltfBrdf(input, LoadVector(address + 48)), 0.0)));
}

// --- quadrature --------------------------------------------------------------------------------

// l을 h로 바꿔 dω_l = 4 (v·h) dω_h로 적분한다. h의 극각 θ_h를 구간으로 나누고 구간마다 midpoint rule로 잰다. 구간을 나누는 법은
// scripts/gltfbrdf.py의 half_angle_bounds와 같다. 적분하는 쪽의 v는 n 아래면 n의 접평면에 대해 거울로 옮긴 것이다. 어떤 v로 l을
// 매개해도 같은 반구를 덮으므로 적분값은 그대로이고, 거울로 옮기면 specular peak 근처의 h가 θ_h = 0 근처에 모인다.
uint FindHalfAngleBounds(float alpha, float viewAngle, out float bounds[kMaxBounds])
{
    const float low = kPi / 4.0 - viewAngle / 2.0;
    const float high = kPi / 4.0 + viewAngle / 2.0;
    uint count = 0;
    bounds[count++] = 0.0;
    float a = alpha, b = low > 0.0 ? low : high;
    // 두 등비수열을 크기 순으로 합친다.
    while (count < kMaxBounds - 1 && (a < high || b < high))
    {
        const float next = min(a, b);
        if (next > bounds[count - 1])
            bounds[count++] = next;
        if (a == next)
            a *= 2.0;
        if (b == next)
            b *= 2.0;
    }
    bounds[count++] = high;
    return count;
}

float FindReflectingAzimuth(float halfAngle, float viewAngle)
{
    const float a = sin(2.0 * halfAngle) * sin(viewAngle);
    const float b = -cos(viewAngle) * cos(2.0 * halfAngle);
    if (a <= 0.0)
        return b < 0.0 ? kPi : 0.0;
    return acos(clamp(b / a, -1.0, 1.0));
}

void IntegrateByQuadrature(uint record, uint thread)
{
    const uint address = record * kRecordBytes;
    const Material material = LoadMaterial(address);
    const float3 tangent = LoadVector(address + 32);
    const float3 view = LoadVector(address + 48);
    const GltfBrdfInput input = MakeGltfBrdfInput(material.baseColor, material.metallic, material.roughness, material.normal, view);

    const float3 normal = material.normal;
    const float3 parameterView = view - 2.0 * min(0.0, dot(normal, view)) * normal;
    const float viewCosine = saturate(dot(normal, parameterView));
    const float viewAngle = acos(viewCosine);
    float3 azimuthAxis = parameterView - viewCosine * normal;
    azimuthAxis = dot(azimuthAxis, azimuthAxis) > 1e-12 ? normalize(azimuthAxis) : tangent;
    const float3 otherAxis = cross(normal, azimuthAxis);
    const float3 localView = float3(sin(viewAngle), 0.0, viewCosine);

    float bounds[kMaxBounds];
    const uint segments = FindHalfAngleBounds(input.alpha, viewAngle, bounds) - 1;
    const uint nodesPerSegment = ThetaNodes * PhiNodes;

    float values[kMaxSums];
    for (uint i = 0; i < kMaxSums; ++i)
        values[i] = 0.0;
    float3 integral = 0.0, weightedIntegral = 0.0;
    float invalid = 0.0, evaluations = 0.0;
    for (uint node = thread; node < segments * nodesPerSegment; node += kGroupSize)
    {
        const uint segment = node / nodesPerSegment;
        const uint thetaIndex = (node % nodesPerSegment) / PhiNodes;
        const uint phiIndex = node % PhiNodes;
        const float width = bounds[segment + 1] - bounds[segment];
        const float halfAngle = bounds[segment] + (thetaIndex + 0.5) * width / ThetaNodes;
        const float azimuthRange = FindReflectingAzimuth(halfAngle, viewAngle);
        if (azimuthRange <= 0.0)
            continue;
        const float azimuth = ((phiIndex + 0.5) / PhiNodes * 2.0 - 1.0) * azimuthRange;

        const float3 localHalf = float3(sin(halfAngle) * cos(azimuth), sin(halfAngle) * sin(azimuth), cos(halfAngle));
        const float viewHalfCosine = dot(localView, localHalf);
        const float3 localLight = 2.0 * viewHalfCosine * localHalf - localView;
        if (localLight.z <= 0.0)
            continue;
        const float3 light = normalize(azimuthAxis * localLight.x + otherAxis * localLight.y + normal * localLight.z);

        const float3 f = EvaluateGltfBrdf(input, light);
        evaluations += 1.0;
        if (!IsValidValue(f))
        {
            invalid += 1.0;
            continue;
        }
        const float measure = 4.0 * viewHalfCosine * sin(halfAngle) * (width / ThetaNodes) * (2.0 * azimuthRange / PhiNodes);
        const float3 contribution = f * (max(0.0, dot(normal, light)) * measure);
        integral += contribution;
        weightedIntegral += contribution * TestWeight(tangent, light);
    }
    values[0] = integral.r; values[1] = integral.g; values[2] = integral.b; values[3] = invalid;
    values[4] = weightedIntegral.r; values[5] = weightedIntegral.g; values[6] = weightedIntegral.b; values[7] = evaluations;
    ReduceGroup(thread, values, 8, 0);
    if (thread == 0)
        StoreSums(record * 32, values, 8);
}

// --- sample ------------------------------------------------------------------------------------

void IntegrateBySampling(uint record, uint thread)
{
    const uint address = record * kRecordBytes;
    const Material material = LoadMaterial(address);
    const float3 tangent = LoadVector(address + 32);
    const uint seed = Records.Load(address + 44);
    const float3 view = LoadVector(address + 48);
    const uint chunk = Records.Load(address + 60);
    const GltfBrdfInput input = MakeGltfBrdfInput(material.baseColor, material.metallic, material.roughness, material.normal, view);

    float3 sum = 0.0, squares = 0.0, weightedSum = 0.0, weightedSquares = 0.0;
    float invalidValues = 0.0, invalidWeights = 0.0, largestLengthError = 0.0;
    for (uint i = 0; i < SamplesPerThread; ++i)
    {
        Random random = MakeRandom(seed, uint2(chunk, thread), i);
        const float3 samples = float3(random.rand(), random.rand2());
        const GltfBrdfSample sample = SampleGltfBrdfDirection(input, samples);

        // 유한하지 않은 방향은 weight가 0이 되어 weight로는 잡히지 않으므로 가장 큰 float로 남긴다.
        const float lengthError = abs(length(sample.direction) - 1.0);
        largestLengthError = max(largestLengthError, isfinite(lengthError) ? lengthError : 3.402823466e+38);
        if (!IsValidValue(EvaluateGltfBrdf(input, sample.direction)))
            invalidValues += 1.0;
        if (!IsValidValue(sample.weight))
        {
            invalidWeights += 1.0;
            continue;
        }
        const float3 weighted = sample.weight * TestWeight(tangent, sample.direction);
        sum += sample.weight;
        squares += sample.weight * sample.weight;
        weightedSum += weighted;
        weightedSquares += weighted * weighted;
    }

    float values[kMaxSums];
    values[0] = sum.r; values[1] = sum.g; values[2] = sum.b; values[3] = invalidValues;
    values[4] = squares.r; values[5] = squares.g; values[6] = squares.b; values[7] = invalidWeights;
    values[8] = weightedSum.r; values[9] = weightedSum.g; values[10] = weightedSum.b; values[11] = largestLengthError;
    values[12] = weightedSquares.r; values[13] = weightedSquares.g; values[14] = weightedSquares.b; values[15] = 0.0;
    ReduceGroup(thread, values, 16, 1u << 11);
    if (thread == 0)
        StoreSums(record * 64, values, 16);
}

[numthreads(kGroupSize, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    if (Mode == kEvaluate)
    {
        const uint record = FirstRecord + group.x * kGroupSize + thread;
        if (record < NumRecords)
            Evaluate(record);
        return;
    }
    const uint record = FirstRecord + group.x;
    if (record >= NumRecords)
        return;
    if (Mode == kQuadrature)
        IntegrateByQuadrature(record, thread);
    else
        IntegrateBySampling(record, thread);
}
