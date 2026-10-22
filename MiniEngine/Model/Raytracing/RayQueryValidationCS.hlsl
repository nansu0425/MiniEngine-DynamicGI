#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"
#include "Model/Raytracing/RayQueryValidationStats.hlsli"

// RayQueryValidation.h의 Mode와 같은 값이다.
static const uint kRayHit = 0;

// mode마다 category와 metric의 순서는 RayQueryValidation.cpp의 label 순서와 같다.
static const uint kRayHitHit = 0;
static const uint kRayHitMiss = 1;
static const uint kRayHitT = 0;

cbuffer Constants : register(b0)
{
    CameraRays Rays;
    uint2 TargetSize;
    uint Mode;
    uint NumCategories;
    uint NumMetrics;
    // RayQueryValidation::Heatmap
    uint HeatmapItem;
    float HeatmapMinLog2;
    float HeatmapMaxLog2;
};

RWTexture2D<float4> Output : register(u0);

// mode가 pixel 하나를 검사한 결과
struct PixelResult
{
    // category 순서번째 bit가 그 category에 들었는지다.
    uint categories;
    // metric 순서번째 bit가 그 metric을 쟀는지다.
    uint measuredMetrics;
    float4 metrics;
};

// camera ray가 맞았는지만 본다. hit와 miss의 합이 pixel 수와 같아야 하므로 집계가 맞는지 확인하는 데 쓴다.
PixelResult ValidateRayHit(uint2 pixel)
{
    PixelResult result = (PixelResult)0;
    RayHit hit;
    if (!TraceClosestHit(GetCameraRay(Rays, pixel), kCullSingleSidedBackFaces, hit))
    {
        result.categories = 1u << kRayHitMiss;
        return result;
    }

    result.categories = 1u << kRayHitHit;
    result.measuredMetrics = 1u << kRayHitT;
    result.metrics[kRayHitT] = hit.t;
    return result;
}

PixelResult Validate(uint2 pixel)
{
    if (Mode == kRayHit)
        return ValidateRayHit(pixel);
    return (PixelResult)0;
}

// category에 든 pixel은 한 색으로 칠하되, 어디인지 알아볼 수 있게 scene이 비치도록 둔다.
static const float3 kCategoryColor = float3(0xeb, 0x68, 0x34) / 255.0;
static const float kCategoryAlpha = 0.8;
// metric은 한 hue의 옅은 색부터 짙은 색까지로 칠해 값이 클수록 짙다.
// 색의 진하기가 곧 값이므로 scene의 밝기가 섞이지 않게 불투명하게 칠한다.
static const float3 kMetricRamp[5] =
{
    float3(0xcd, 0xe2, 0xfb) / 255.0,
    float3(0x86, 0xb6, 0xef) / 255.0,
    float3(0x39, 0x87, 0xe5) / 255.0,
    float3(0x1c, 0x5c, 0xab) / 255.0,
    float3(0x0d, 0x36, 0x6b) / 255.0
};

// overlay는 premultiplied alpha로 scene 위에 섞인다.
float4 Premultiply(float3 color, float alpha)
{
    return float4(color * alpha, alpha);
}

float3 MetricColor(float value)
{
    const float position = saturate((log2(value) - HeatmapMinLog2) / (HeatmapMaxLog2 - HeatmapMinLog2)) * 4.0;
    const uint step = min(uint(position), 3);
    return lerp(kMetricRamp[step], kMetricRamp[step + 1], position - step);
}

// 칠할 것이 없는 pixel은 투명하게 두어 raster가 보이게 한다.
float4 HeatmapColor(PixelResult result)
{
    if (HeatmapItem < NumCategories)
        return ((result.categories >> HeatmapItem) & 1) != 0 ? Premultiply(kCategoryColor, kCategoryAlpha) : 0.0;

    const uint metric = HeatmapItem - NumCategories;
    return ((result.measuredMetrics >> metric) & 1) != 0 ? Premultiply(MetricColor(result.metrics[metric]), 1.0) : 0.0;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= TargetSize))
        return;

    const PixelResult result = Validate(pixel);

    CountPixel();
    CountCategories(result.categories, NumCategories);
    for (uint metric = 0; metric < NumMetrics; ++metric)
    {
        if (((result.measuredMetrics >> metric) & 1) != 0)
            AddMetric(metric, result.metrics[metric]);
    }

    Output[pixel] = HeatmapColor(result);
}
