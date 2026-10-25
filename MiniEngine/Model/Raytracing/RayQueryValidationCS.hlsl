#include "Model/Raytracing/CameraRays.hlsli"
#include "Model/Raytracing/RaytracingScene.hlsli"
#include "Model/Raytracing/RayQueryValidationStats.hlsli"

// RayQueryValidation.h의 Mode와 같은 값이다.
static const uint kDepthComparison = 0;

// mode마다 category와 metric의 순서는 RayQueryValidation.cpp의 label 순서와 같다.
static const uint kDepthMismatch = 0;
static const uint kRasterOnlyHit = 1;
static const uint kRayOnlyHit = 2;
static const uint kAlphaTestMismatch = 3;
static const uint kDepthMatch = 4;
static const uint kBothMiss = 5;
static const uint kDepthError = 0;

// 정상적인 raster와 ray의 Depth Error보다 충분히 크면서, 두 sample 위치가 반 pixel 어긋날 때의 오차는 잡는 값이다.
// scripts/viewer.py가 이 이름과 형식으로 읽어 pick의 depth를 raster depth와 견준다.
static const float kDepthMatchTolerance = 1e-4;

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
    // raster를 그린 camera
    float3 CameraForward;
    float NearZ;
    float FarZ;
};

// raster가 그린 depth buffer
Texture2D<float> RasterDepth : register(t0);

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

// raster depth와 ray depth를 같은 pixel에서 비교한다. category는 서로 겹치지 않아 합이 pixel 수와 같다.
// D32 reverse Z의 저장 오차와 raster의 subpixel snap 오차가 거리에 비례하므로, Depth Error는 raster depth에 대한 상대 오차로 재서
// threshold 하나로 가까운 곳과 먼 곳을 같이 판정한다.
PixelResult ValidateDepthComparison(uint2 pixel)
{
    PixelResult result = (PixelResult)0;

    RayDesc ray = GetRasterSampleRay(Rays, pixel);
    ClipToRasterDepthRange(ray, CameraForward, NearZ, FarZ);
    const float cosine = dot(ray.Direction, CameraForward);

    // depth buffer는 far plane인 0으로 지워진다.
    const float rasterZ = RasterDepth[pixel];
    const bool rasterHit = rasterZ != 0.0;
    RayHit closestHit;
    const bool rayHit = TraceClosestHit(ray, kCullSingleSidedBackFaces, closestHit);
    if (!rasterHit && !rayHit)
    {
        result.categories = 1u << kBothMiss;
        return result;
    }

    const bool decidedByAlphaTest = IsDecidedByAlphaTest(ray, kCullSingleSidedBackFaces, rayHit, closestHit);
    if (rasterHit && rayHit)
    {
        const float rasterDepth = GetRasterViewDepth(rasterZ, NearZ, FarZ);
        const float error = abs(closestHit.t * cosine - rasterDepth) / rasterDepth;
        // raster의 cutout은 mip chain을, ray는 mip 0을 읽으므로 alpha test가 정한 pixel은 threshold를 정할 분포에서 뺀다.
        if (!decidedByAlphaTest)
        {
            result.measuredMetrics = 1u << kDepthError;
            result.metrics[kDepthError] = error;
        }
        if (error <= kDepthMatchTolerance)
        {
            result.categories = 1u << kDepthMatch;
            return result;
        }
    }

    if (decidedByAlphaTest)
        result.categories = 1u << kAlphaTestMismatch;
    else if (rasterHit && rayHit)
        result.categories = 1u << kDepthMismatch;
    else if (rasterHit)
        result.categories = 1u << kRasterOnlyHit;
    else
        result.categories = 1u << kRayOnlyHit;
    return result;
}

PixelResult Validate(uint2 pixel)
{
    if (Mode == kDepthComparison)
        return ValidateDepthComparison(pixel);
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
