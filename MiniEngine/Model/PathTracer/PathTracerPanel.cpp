#include "Model/PathTracer/PathTracerPanel.h"
#include "Model/PathTracer/PathTracer.h"
#include "Core/pch.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/EditorSettings.h"
#include "Core/UI/ImGuiLayer.h"

#include <imgui.h>
#include <algorithm>

namespace
{
    const char* kSettingsSection = "Path Tracer";

    constexpr uint32_t kMaxSamplesPerFrame = 256;
    // 누적 buffer가 sample 수를 float로 세므로 1씩 더해도 정확한 2^24보다 작게 둔다.
    constexpr uint32_t kMaxTargetSamples = 1u << 20;
    // albedo가 1인 furnace는 Russian roulette로도 끊기지 않으므로, 한 dispatch가 끝나는 시간을 이 상한이 정한다.
    // Sponza의 albedo 1 furnace에서는 4096번 반사해도 escape하지 못하는 path가 남아서 그보다 넉넉히 둔다.
    constexpr uint32_t kMaxBounces = 1u << 16;

    bool s_ShowPanel = false;
    bool s_Show = false;
    PathTracer::Settings s_Settings;

    // pt wait samples=<n>이 기다리는 sample 수와 그 응답
    struct SampleWait
    {
        uint32_t samples;
        ControlChannel::Reply reply;
    };
    std::vector<SampleWait> s_SampleWaits;

    void ClampSettings(void)
    {
        s_Settings.samplesPerFrame = std::max(1u, std::min(s_Settings.samplesPerFrame, kMaxSamplesPerFrame));
        s_Settings.targetSamples = std::max(1u, std::min(s_Settings.targetSamples, kMaxTargetSamples));
        s_Settings.maxBounces = std::min(s_Settings.maxBounces, kMaxBounces);
        // inf는 막지 않는다. scripts/check-path-tracer-export.py가 inf로 두어 NaN과 Inf인 sample을 만든다.
        s_Settings.skyRadiance = std::max(0.0f, s_Settings.skyRadiance);
        // albedo가 1을 넘으면 반사할 때마다 energy가 늘어 path의 값이 반사 횟수의 상한을 따라 끝없이 커진다.
        s_Settings.furnaceAlbedo = std::max(0.0f, std::min(s_Settings.furnaceAlbedo, 1.0f));
        s_Settings.furnaceEmission = std::max(0.0f, s_Settings.furnaceEmission);
    }

    void ShowSettings(void)
    {
        ImGui::Checkbox("Show", &s_Show);
        ImGui::SetItemTooltip("Draws the path tracer over the raster scene before post effects. Off, it stops accumulating and keeps what it has.");

        const uint32_t step = 1;
        ImGui::InputScalar("Samples per Frame", ImGuiDataType_U32, &s_Settings.samplesPerFrame, &step);
        ImGui::InputScalar("Target Samples", ImGuiDataType_U32, &s_Settings.targetSamples);
        ImGui::SetItemTooltip("Stops tracing at this many samples per pixel and keeps showing the average.");
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &s_Settings.seed, &step);
        ImGui::SetItemTooltip("Accumulations that differ only in the seed are independent estimates of the same image.");
        ImGui::InputScalar("Max Bounces", ImGuiDataType_U32, &s_Settings.maxBounces, &step);
        ImGui::SetItemTooltip("Most reflections a path makes. 0 sees only the emission of the first hit and the sky seen directly, "
            "and 1 adds the directional light reflected there.");
        ImGui::Checkbox("Russian Roulette", &s_Settings.russianRoulette);
        ImGui::SetItemTooltip("After the first few reflections, ends paths at random by their throughput and weights the survivors up. Unbiased.");
        ImGui::InputFloat("Sky Radiance", &s_Settings.skyRadiance);
        ImGui::SetItemTooltip("Radiance of the sky in every direction, in cd/m2.");

        ImGui::SeparatorText("Validation");
        ImGui::Checkbox("Geometric Normals", &s_Settings.geometricNormals);
        ImGui::SetItemTooltip("Reflects about the geometric normal instead of the shading normal, "
            "so no energy is lost to directions below the geometric normal.");
        ImGui::Checkbox("Furnace", &s_Settings.furnace);
        ImGui::SetItemTooltip("Gives every surface the albedo and emission below and turns off the directional light. "
            "With the constant sky, the answer is known.");
        ImGui::InputFloat("Furnace Albedo", &s_Settings.furnaceAlbedo);
        ImGui::InputFloat("Furnace Emission", &s_Settings.furnaceEmission);
        ImGui::SetItemTooltip("In cd/m2. With albedo a < 1 and the sky at emission / (1 - a), every pixel is the sky radiance.");
        ClampSettings();

        if (ImGui::Button("Reset"))
            PathTracer::Reset();
        ImGui::SetItemTooltip("Starts over. Moving the camera, resizing the scene or changing a setting above other than the sample counts also starts over.");
    }

    void ShowProgress(void)
    {
        ImGui::SeparatorText("Progress");
        const PathTracer::Progress progress = PathTracer::GetProgress();
        ImGui::Text("Samples: %u / %u", progress.samples, s_Settings.targetSamples);
        ImGui::Text("Time: %.2f s", progress.seconds);
        ImGui::Text("Rate: %.1f samples/s", progress.seconds > 0.0 ? progress.samples / progress.seconds : 0.0);
        ImGui::SetItemTooltip("The GPU time of the pass is the Path Tracer row of the Profiler (Profile build).");
    }

    std::string FormatFlag(bool value)
    {
        return value ? "1" : "0";
    }

    std::string FormatFloat(float value)
    {
        char text[32];
        sprintf_s(text, "%.9g", value);
        return text;
    }

    void SaveSettings(EditorSettings::Values& values)
    {
        values["Show"] = FormatFlag(s_Show);
        values["Samples per Frame"] = std::to_string(s_Settings.samplesPerFrame);
        values["Target Samples"] = std::to_string(s_Settings.targetSamples);
        values["Seed"] = std::to_string(s_Settings.seed);
        values["Max Bounces"] = std::to_string(s_Settings.maxBounces);
        values["Russian Roulette"] = FormatFlag(s_Settings.russianRoulette);
        values["Sky Radiance"] = FormatFloat(s_Settings.skyRadiance);
        values["Geometric Normals"] = FormatFlag(s_Settings.geometricNormals);
        values["Furnace"] = FormatFlag(s_Settings.furnace);
        values["Furnace Albedo"] = FormatFloat(s_Settings.furnaceAlbedo);
        values["Furnace Emission"] = FormatFloat(s_Settings.furnaceEmission);
    }

    void ReadFlag(const EditorSettings::Values& values, const char* key, bool& value)
    {
        const auto text = values.find(key);
        if (text != values.end() && (text->second == "0" || text->second == "1"))
            value = text->second == "1";
    }

    void ReadUInt(const EditorSettings::Values& values, const char* key, uint32_t& value)
    {
        const auto text = values.find(key);
        uint32_t read = 0;
        if (text != values.end() && sscanf_s(text->second.c_str(), "%u", &read) == 1)
            value = read;
    }

    void ReadFloat(const EditorSettings::Values& values, const char* key, float& value)
    {
        const auto text = values.find(key);
        float read = 0.0f;
        if (text != values.end() && sscanf_s(text->second.c_str(), "%f", &read) == 1)
            value = read;
    }

    void LoadSettings(const EditorSettings::Values& values)
    {
        ReadFlag(values, "Show", s_Show);
        ReadUInt(values, "Samples per Frame", s_Settings.samplesPerFrame);
        ReadUInt(values, "Target Samples", s_Settings.targetSamples);
        ReadUInt(values, "Seed", s_Settings.seed);
        ReadUInt(values, "Max Bounces", s_Settings.maxBounces);
        ReadFlag(values, "Russian Roulette", s_Settings.russianRoulette);
        ReadFloat(values, "Sky Radiance", s_Settings.skyRadiance);
        ReadFlag(values, "Geometric Normals", s_Settings.geometricNormals);
        ReadFlag(values, "Furnace", s_Settings.furnace);
        ReadFloat(values, "Furnace Albedo", s_Settings.furnaceAlbedo);
        ReadFloat(values, "Furnace Emission", s_Settings.furnaceEmission);
        ClampSettings();
    }

    ControlChannel::Fields DescribeProgress(void)
    {
        const PathTracer::Progress progress = PathTracer::GetProgress();
        return ControlChannel::Fields()
            .Flag("show", s_Show)
            .Integer("samples", progress.samples)
            .Integer("target", s_Settings.targetSamples)
            .Float("seconds", (float)progress.seconds)
            .Integer("resets", progress.resets);
    }

    // 이 sample 수에 이를 수 없으면 그 이유를 돌려준다.
    const char* FindWhyUnreachable(uint32_t samples)
    {
        if (!s_Show)
            return "Show is off, so the path tracer does not accumulate.";
        if (samples > s_Settings.targetSamples)
            return "the target is lower, so the path tracer stops before that.";
        return nullptr;
    }

    // Render가 앞 frame에 쌓은 sample 수로 답한다. 그 frame은 이미 present되었다.
    void AnswerSampleWaits(void)
    {
        const uint32_t samples = PathTracer::GetProgress().samples;
        s_SampleWaits.erase(std::remove_if(s_SampleWaits.begin(), s_SampleWaits.end(), [samples](const SampleWait& wait)
        {
            if (samples >= wait.samples)
            {
                wait.reply.Ok(DescribeProgress());
                return true;
            }
            if (const char* reason = FindWhyUnreachable(wait.samples))
            {
                wait.reply.Error(reason, DescribeProgress());
                return true;
            }
            return false;
        }), s_SampleWaits.end());
    }

    void RegisterCommands(void)
    {
        ControlChannel::Register("pt", "pt status | pt reset | pt wait samples=<n>: the path tracer's progress. "
            "reset starts over on the next frame that shows the path tracer, and while Show is on replies with state pt after that frame. "
            "wait replies once it has accumulated n samples per pixel",
            [](const std::string& arguments, const ControlChannel::Reply& reply)
        {
            uint32_t samples = 0;
            if (arguments == "status")
            {
                reply.Ok(DescribeProgress());
            }
            else if (arguments == "reset")
            {
                PathTracer::Reset();
                const auto replyWithProgress = [reply] { reply.Ok(ControlChannel::Fields().Object("state", ControlChannel::GetState("pt"))); };
                if (s_Show)
                    ControlChannel::AfterFrames(1, replyWithProgress);
                else
                    replyWithProgress();
            }
            else if (sscanf_s(arguments.c_str(), "wait samples=%u", &samples) == 1)
            {
                if (const char* reason = FindWhyUnreachable(samples))
                    reply.Error(reason, DescribeProgress());
                else
                    s_SampleWaits.push_back({ samples, reply });
            }
            else
            {
                reply.Error("usage: pt status | pt reset | pt wait samples=<n>");
            }
        });
    }
}

void PathTracerPanel::Initialize(void)
{
    PathTracer::Initialize();
    EditorSettings::Register(kSettingsSection, SaveSettings, LoadSettings);
    RegisterCommands();
    ControlChannel::RegisterState("pt", DescribeProgress);
}

void PathTracerPanel::Shutdown(void)
{
    s_SampleWaits.clear();
    EditorSettings::Unregister(kSettingsSection);
    PathTracer::Shutdown();
}

void PathTracerPanel::Update(void)
{
    AnswerSampleWaits();
    ImGuiLayer::ShowPanel("Path Tracer", s_ShowPanel, []
    {
        ShowSettings();
        ShowProgress();
    });
}

void PathTracerPanel::Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, const Math::Vector3& lightDirection, const Math::Vector3& lightIlluminance, ColorBuffer& sceneColor)
{
    if (s_Show)
        PathTracer::Render(context, s_Settings, camera, rasterViewport, scene, lightDirection, lightIlluminance, sceneColor);
    PathTracer::Capture(context);
}
