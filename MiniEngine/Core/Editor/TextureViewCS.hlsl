#include "Core/Shaders/ColorSpaceUtility.hlsli"

cbuffer Constants : register(b0)
{
    float ExposureScale;
    uint3 SourceComponents;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Display : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint width, height;
    Display.GetDimensions(width, height);
    if (DTid.x >= width || DTid.y >= height)
        return;

    const float4 source = Source[DTid];
    const float3 selected = float3(source[SourceComponents.x], source[SourceComponents.y], source[SourceComponents.z]);
    Display[DTid] = float4(ApplySRGBCurve(saturate(selected * ExposureScale)), 1.0);
}
