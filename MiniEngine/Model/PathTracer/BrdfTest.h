#pragma once

// harness가 Core/Shaders/GltfBrdf.hlsli의 함수를 GPU에서 돌려 보는 brdf 명령. path tracer와 raster가 부르는 바로 그 함수를 파일로 받은
// 기록마다 부르고 그 결과를 파일로 돌려준다. 판정은 scripts/check-gltf-brdf.py가 한다.
namespace BrdfTest
{
    void Initialize(void);
}
