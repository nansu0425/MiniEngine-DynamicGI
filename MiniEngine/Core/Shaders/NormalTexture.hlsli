#ifndef __NORMAL_TEXTURE_HLSLI__
#define __NORMAL_TEXTURE_HLSLI__

// cross(surfaceNormal, tangent)의 길이가 이 값보다 작으면 tangent space가 정의되지 않는다고 본다. 길이는 두 단위 벡터가 이루는 각의
// sin이고, 2^-16은 약 0.0009°의 각이다. float32로 정규화한 두 벡터의 cross는 성분마다 2^-20쯤 틀릴 수 있다 (GPU의 rsqrt 오차를 넣은
// 어림). 그래서 길이가 2^-16보다 짧은 cross로 만든 bitangent는 방향이 1/16 rad 넘게 틀릴 수 있다.
static const float kMinTangentCrossLength = 1.0 / 65536.0;

// surfaceNormal은 world 공간의 단위 normal이다. tangent의 xyz는 world 공간의 tangent이고, w는 bitangent의 부호 (glTF의 TANGENT)다.
// normalTextureValue는 normal texture에서 읽은 rgb다. tangent frame의 세 축이 직교하지 않을 수 있어서, 돌려주는 normal의 길이는 1이 아닐 수 있다.
//
// tangent space가 정의되지 않으면 normal texture를 쓰지 않고 surfaceNormal을 그대로 돌려준다. TANGENT의 xyz가 NORMAL과 평행하면 bitangent
// 식 (references/glTF/specification/2.0/Specification.adoc:1763)의 cross가 0이 되어 tangent frame을 만들 수 없다. spec은 TANGENT의 xyz가
// 단위 벡터이고 w가 ±1이라고만 정하므로 (같은 파일:1602) 이런 asset도 spec을 따른다. spec은 이때 normal texture를 어떻게 쓸지 정하지
// 않는다. 이 repo는 normalTexture가 없으면 surface normal을 그대로 쓴다는 규칙 (같은 파일:2760)을 따라, tangent frame이 없는 곳에는 normal
// texture가 없는 것으로 본다. spec이 tangent space가 정의되지 않는다고 부르는 경우 (한 triangle의 W가 다를 때, 같은 파일:1761)는 여기서
// 다루지 않는다.
float3 GetNormalTextureNormal(float3 surfaceNormal, float4 tangent, float3 normalTextureValue, float normalTextureScale)
{
    const float3 unitTangent = normalize(tangent.xyz);
    const float3 tangentCross = cross(surfaceNormal, unitTangent);
    if (dot(tangentCross, tangentCross) < kMinTangentCrossLength * kMinTangentCrossLength)
        return surfaceNormal;

    const float3 bitangent = normalize(tangentCross) * tangent.w;
    float3 textureNormal = normalTextureValue * 2.0 - 1.0;
    textureNormal = normalize(textureNormal * float3(normalTextureScale, normalTextureScale, 1.0));
    return mul(textureNormal, float3x3(unitTangent, bitangent, surfaceNormal));
}

#endif
