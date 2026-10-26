#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

class ComputeContext;
class ColorBuffer;
class DepthBuffer;
class RaytracingScene;
struct D3D12_VIEWPORT;

namespace Math
{
    class Camera;
}

namespace ControlChannel
{
    class Reply;
}

// ray tracing infra를 검증하는 compute pass. pixel마다 camera ray로 mode가 정한 것을 검사해 결과를 heatmap으로 칠하고,
// 화면 전체의 결과를 category별 count와 metric별 log2 histogram, max로 모은다.
namespace RayQueryValidation
{
    // RayQueryValidationCS.hlsl의 Mode와 같은 값이다.
    enum Mode { kDepthComparison, kNumModes };
    const char* GetModeLabel(Mode mode);

    // overlay에 칠할 것과 칠하는 범위
    struct Heatmap
    {
        // mode의 category를 먼저, 그 다음 metric을 센 순서
        int item;
        // metric을 칠하는 log2 범위. 아래 끝은 옅은 파랑, 위 끝은 짙은 파랑이다.
        int minExponent;
        int maxExponent;
    };
    // metric을 칠하는 범위로 고를 수 있는 log2 값. stats의 histogram이 나누는 범위와 같다.
    constexpr int kMinHeatmapExponent = -32;
    constexpr int kMaxHeatmapExponent = 32;

    int GetNumHeatmapItems(Mode mode);
    const char* GetHeatmapItemLabel(Mode mode, int item);
    bool IsMetricItem(Mode mode, int item);
    // metric item은 그 metric이 가질 수 있는 값을 넓게 덮는 범위에서 시작한다.
    Heatmap GetDefaultHeatmap(Mode mode, int item);
    // 마지막 Capture가 같은 mode에서 heatmap의 metric을 쟀으면, 범위를 그 metric의 p1부터 p99까지로 맞추고 true를 돌려준다.
    // 위 1%는 가장 짙은 색이 되어 눈에 띈다.
    bool FitHeatmapToCapture(Mode mode, Heatmap& heatmap);
    // validation fit이 panel의 Fit to Capture와 같은 일을 하게 한다. heatmap은 고른 view와 함께 RaytracingView가 가지므로 그쪽이 정하고 응답한다.
    void SetFitCommand(const std::function<void(const ControlChannel::Reply&)>& fit);

    // Log가 수치와 함께 한 줄에 찍고 Restore가 그 줄에서 되돌리는 장면의 조건. key와 value에는 '='와 ';'를 쓰지 않는다.
    typedef std::map<std::string, std::string> SceneConditions;
    // camera와 directional light처럼 app이 가진 조건은 app이 정한다. restore는 필요한 key가 없으면 false를 돌려준다.
    // statePaths는 그 조건이 있는 state의 path이고, restore 명령이 바꾼 것을 응답할 때 쓴다. 정하지 않으면 Restore는 비활성이다.
    void SetSceneConditions(const std::function<void(SceneConditions&)>& describe, const std::function<bool(const SceneConditions&)>& restore,
        const std::vector<std::string>& statePaths);

    void Initialize(void);
    void Shutdown(void);
    // Ray Tracing panel의 Validation section 내용. Capture는 바로 앞 frame의 집계를 읽어 보여 주고,
    // Log는 그 수치를 측정 조건과 함께 한 줄로 찍으며, Restore는 그 한 줄의 조건으로 되돌린다.
    void ShowSection(void);
    // camera ray는 rasterViewport로 그린 raster의 sample 위치를 지난다. rasterDepth는 그 raster가 그린 depth buffer이고 target과 크기가 같다.
    void Render(ComputeContext& context, Mode mode, const Heatmap& heatmap, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
        DepthBuffer& rasterDepth, const RaytracingScene& scene, ColorBuffer& target);
}
