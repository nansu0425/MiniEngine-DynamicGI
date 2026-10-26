#pragma once

class ComputeContext;

// TAA, motion blur, depth of field가 읽는 이번 frame의 g_LinearDepth를 scene depth로 채운다.
// SSAO를 켜면 SSAO가 같은 값을 채우므로 이것을 부르지 않아도 된다.
namespace LinearDepth
{
    void Initialize(void);

    // depth pre-pass 뒤, g_LinearDepth를 읽는 pass 앞에서 부른다.
    void Update(ComputeContext& context, float nearClip, float farClip);
}
