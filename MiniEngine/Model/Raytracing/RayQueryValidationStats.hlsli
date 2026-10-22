#ifndef __RAY_QUERY_VALIDATION_STATS_HLSLI__
#define __RAY_QUERY_VALIDATION_STATS_HLSLI__

// pixel마다의 검증 결과를 화면 전체에 대해 모은다. 상수와 layout은 RayQueryValidation.cpp의 Stats와 같다.
//   uint pixelCount
//   uint categoryCounts[kMaxCategories]
//   metric마다 { uint maxBits; uint bins[kNumBins]; }

static const uint kMaxCategories = 32;
static const uint kMaxMetrics = 4;

// histogram은 2^kMinExponent부터 kNumOctaves개의 octave를 octave마다 2^kBinsPerOctaveLog2개의 bin으로 나눈다.
// 범위 밖의 값과 0, NaN과 Inf는 따로 센다.
static const int kMinExponent = -32;
static const uint kNumOctaves = 64;
static const uint kBinsPerOctaveLog2 = 2;
static const uint kNumRangeBins = kNumOctaves << kBinsPerOctaveLog2;
static const uint kZeroBin = 0;
static const uint kBelowRangeBin = 1;
static const uint kFirstRangeBin = 2;
static const uint kAboveRangeBin = kFirstRangeBin + kNumRangeBins;
static const uint kNonFiniteBin = kAboveRangeBin + 1;
static const uint kNumBins = kNonFiniteBin + 1;

static const uint kPixelCountAddress = 0;
static const uint kCategoryCountsAddress = 4;
static const uint kMetricsAddress = kCategoryCountsAddress + kMaxCategories * 4;
static const uint kMetricStride = (1 + kNumBins) * 4;

RWByteAddressBuffer Stats : register(u1);

// 양수 float의 bit는 크기 순서와 같으므로, exponent와 mantissa의 위 bit를 이은 값이 그대로 log2 scale의 bin 번호가 된다.
uint HistogramBin(float value)
{
    if (value == 0.0)
        return kZeroBin;
    if (!isfinite(value))
        return kNonFiniteBin;

    const int rangeBin = int(asuint(value) >> (23 - kBinsPerOctaveLog2)) - ((127 + kMinExponent) << kBinsPerOctaveLog2);
    if (rangeBin < 0)
        return kBelowRangeBin;
    if (rangeBin >= int(kNumRangeBins))
        return kAboveRangeBin;
    return kFirstRangeBin + rangeBin;
}

uint CountLanes(uint4 laneMask)
{
    return countbits(laneMask.x) + countbits(laneMask.y) + countbits(laneMask.z) + countbits(laneMask.w);
}

uint FirstLane(uint4 laneMask)
{
    if (laneMask.x != 0)
        return firstbitlow(laneMask.x);
    if (laneMask.y != 0)
        return 32 + firstbitlow(laneMask.y);
    if (laneMask.z != 0)
        return 64 + firstbitlow(laneMask.z);
    return 96 + firstbitlow(laneMask.w);
}

// 아래 함수들은 같은 주소에 몰리는 atomic을 줄이려고 wave 안에서 먼저 모은 뒤 lane 하나만 더한다.
// 그래서 wave의 active lane이 모두 함께 불러야 하고, 부른 lane만 센다.

void CountPixel(void)
{
    const uint count = WaveActiveCountBits(true);
    if (WaveIsFirstLane())
        Stats.InterlockedAdd(kPixelCountAddress, count);
}

// categories의 category 순서번째 bit가 그 category에 들었는지다.
void CountCategories(uint categories, uint numCategories)
{
    for (uint category = 0; category < numCategories; ++category)
    {
        const uint count = WaveActiveCountBits(((categories >> category) & 1) != 0);
        if (WaveIsFirstLane() && count != 0)
            Stats.InterlockedAdd(kCategoryCountsAddress + category * 4, count);
    }
}

// value는 0 이상이어야 한다. 음이 아닌 float는 bit의 순서가 크기 순서와 같아 uint로 max를 구한다.
void AddMetric(uint metric, float value)
{
    const uint address = kMetricsAddress + metric * kMetricStride;

    const uint maxBits = WaveActiveMax(asuint(value));
    if (WaveIsFirstLane())
        Stats.InterlockedMax(address, maxBits);

    const uint bin = HistogramBin(value);
    const uint4 sameBin = WaveMatch(bin);
    if (WaveGetLaneIndex() == FirstLane(sameBin))
        Stats.InterlockedAdd(address + 4 + bin * 4, CountLanes(sameBin));
}

#endif
