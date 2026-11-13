#include "Core/Shaders/ColorSpaceUtility.hlsli"

cbuffer Constants : register(b0)
{
    float ExposureScale;
    uint3 SourceComponents;
    float2 TexelOrigin;
    float TexelsPerPixel;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Display : register(u0);

static const float3 kOutsideTextureColor = 0.1;

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    // Display texture를 벗어나는 thread 종료
    uint displayWidth, displayHeight;
    Display.GetDimensions(displayWidth, displayHeight);
    if (DTid.x >= displayWidth || DTid.y >= displayHeight)
        return;

    // pixe에 해당하는 texel 좌표 계산
    uint sourceWidth, sourceHeight;
    Source.GetDimensions(sourceWidth, sourceHeight);
    const float2 texel = floor(TexelOrigin + (DTid + 0.5) * TexelsPerPixel);
    
    // source texture를 벗어나는 pixel    
    if (any(texel < 0.0) || texel.x >= sourceWidth || texel.y >= sourceHeight)
    {
        Display[DTid] = float4(kOutsideTextureColor, 1.0);
        return;
    }

    // texel 값에 Exporsure scale을 곱하고 sRGB encoding
    const float4 source = Source[uint2(texel)];
    const float3 selected = float3(source[SourceComponents.x], source[SourceComponents.y], source[SourceComponents.z]);
    Display[DTid] = float4(ApplySRGBCurve(saturate(selected * ExposureScale)), 1.0);
}
