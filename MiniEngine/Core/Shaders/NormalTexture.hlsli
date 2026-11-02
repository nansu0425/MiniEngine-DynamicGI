// normal texture로 shading normal을 구하는 식. raster의 DefaultPS와 ray의 GetHitSurface가 함께 쓴다.

#ifndef __NORMAL_TEXTURE_HLSLI__
#define __NORMAL_TEXTURE_HLSLI__

// surfaceNormal은 world 공간의 단위 normal이다. tangent의 xyz는 world 공간의 tangent이고, w는 bitangent의 부호 (glTF의 TANGENT)다.
// normalTextureValue는 normal texture에서 읽은 rgb다. tangent frame의 세 축이 직교하지 않을 수 있어서, 돌려주는 normal의 길이는 1이 아닐 수 있다.
float3 GetNormalTextureNormal(float3 surfaceNormal, float4 tangent, float3 normalTextureValue, float normalTextureScale)
{
    const float3 unitTangent = normalize(tangent.xyz);
    const float3 bitangent = normalize(cross(surfaceNormal, unitTangent)) * tangent.w;
    float3 textureNormal = normalTextureValue * 2.0 - 1.0;
    textureNormal = normalize(textureNormal * float3(normalTextureScale, normalTextureScale, 1.0));
    return mul(textureNormal, float3x3(unitTangent, bitangent, surfaceNormal));
}

#endif
