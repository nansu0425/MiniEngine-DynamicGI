#pragma once

class ComputeContext;
class ColorBuffer;
class RaytracingScene;
class ShadowBuffer;
struct D3D12_VIEWPORT;

namespace Math
{
    class Camera;
    class Matrix4;
    class Vector3;
}

// path tracer의 설정과 진행을 보이는 panel. 설정은 editor.ini의 "Path Tracer" section에 저장하고, harness는 pt 명령으로 진행을 읽는다.
namespace PathTracerPanel
{
    void Initialize(void);
    void Shutdown(void);
    // ImGuiLayer::BeginFrame과 Render 사이에서 부른다.
    void Update(void);
    // Show를 끄면 누적을 멈추고 누적한 내용은 남긴다. 인자의 뜻은 PathTracer::Render와 같다.
    void Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        const RaytracingScene& scene, const Math::Vector3& lightDirection, const Math::Vector3& lightIlluminance, const Math::Vector3& skyRadiance,
        ShadowBuffer& shadowMap, const Math::Matrix4& shadowMatrix, ColorBuffer& sceneColor);
}
