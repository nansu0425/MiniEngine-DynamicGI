#include "Model/PathTracer/PathTracerPanel.h"
#include "Model/PathTracer/PathTracer.h"
#include "Core/pch.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Editor/EditorSettings.h"
#include "Core/Editor/MeasurementConditions.h"
#include "Core/UI/ImGuiLayer.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace
{
    const char* kSettingsSection = "Path Tracer";
    const char* kLogPrefix = "[Path Tracer] ";

    constexpr uint32_t kMaxSamplesPerFrame = 256;
    // 누적 buffer가 sample 수를 float로 세므로 1씩 더해도 정확한 2^24보다 작게 둔다.
    constexpr uint32_t kMaxTargetSamples = 1u << 20;
    // albedo가 1인 furnace는 Russian roulette로도 끊기지 않으므로, 한 dispatch가 끝나는 시간을 이 상한이 정한다.
    // Sponza의 albedo 1 furnace에서는 4096번 반사해도 escape하지 못하는 path가 남아서 그보다 넉넉히 둔다.
    constexpr uint32_t kMaxBounces = 1u << 16;

    bool s_ShowPanel = false;
    bool s_Show = false;
    PathTracer::Settings s_Settings;

    // PathTracer::Material의 순서이고, editor.ini와 settings 명령이 이 이름으로 읽고 쓴다.
    const char* kMaterialNames[] = { "glTF", "Lambert" };

    // pt wait samples=<n>이 기다리는 sample 수와 그 응답
    struct SampleWait
    {
        uint32_t samples;
        ControlChannel::Reply reply;
    };
    std::vector<SampleWait> s_SampleWaits;

    // Log하면 그 줄로 채운다.
    MeasurementConditions::RestoreField s_RestoreField;

    void ClampSettings(void)
    {
        s_Settings.samplesPerFrame = std::max(1u, std::min(s_Settings.samplesPerFrame, kMaxSamplesPerFrame));
        s_Settings.targetSamples = std::max(1u, std::min(s_Settings.targetSamples, kMaxTargetSamples));
        s_Settings.maxBounces = std::min(s_Settings.maxBounces, kMaxBounces);
        // albedo가 1을 넘으면 반사할 때마다 energy가 늘어 path의 값이 반사 횟수의 상한을 따라 끝없이 커진다.
        s_Settings.furnaceAlbedo = std::max(0.0f, std::min(s_Settings.furnaceAlbedo, 1.0f));
        s_Settings.furnaceEmission = std::max(0.0f, s_Settings.furnaceEmission);
    }

    void ShowSettings(void)
    {
        ImGui::Checkbox("Show", &s_Show);
        ImGui::SetItemTooltip("Draws the path tracer over the raster scene before post effects. Off, it stops accumulating and keeps what it has.");

        int material = (int)s_Settings.material;
        if (ImGui::Combo("Material", &material, kMaterialNames, IM_ARRAYSIZE(kMaterialNames)))
            s_Settings.material = (PathTracer::Material)material;
        ImGui::SetItemTooltip("glTF reflects by the glTF metallic-roughness BRDF, the answer to compare with. Lambert reflects diffusely "
            "with albedo base color x (1 - metallic), so the furnaces have answers to the bit.");

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

        ImGui::SeparatorText("Validation");
        ImGui::Checkbox("Geometric Normals", &s_Settings.geometricNormals);
        ImGui::SetItemTooltip("Reflects about the geometric normal instead of the shading normal, "
            "so no energy is lost to directions below the geometric normal.");
        ImGui::Checkbox("Furnace", &s_Settings.furnace);
        ImGui::SetItemTooltip("Gives every surface the albedo and emission below and turns off the directional light. "
            "As the Sky Light is the same in every direction, the answer is known. With glTF the albedo scales the BRDF of base color 1, "
            "metallic 0 and the material's roughness, which reflects all light from every direction.");
        ImGui::InputFloat("Furnace Albedo", &s_Settings.furnaceAlbedo);
        ImGui::InputFloat("Furnace Emission", &s_Settings.furnaceEmission);
        ImGui::SetItemTooltip("In cd/m2. With albedo a < 1 and the Sky Light at emission / (1 - a), every pixel is the Sky Light's radiance.");
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
        values["Material"] = kMaterialNames[(int)s_Settings.material];
        values["Samples per Frame"] = std::to_string(s_Settings.samplesPerFrame);
        values["Target Samples"] = std::to_string(s_Settings.targetSamples);
        values["Seed"] = std::to_string(s_Settings.seed);
        values["Max Bounces"] = std::to_string(s_Settings.maxBounces);
        values["Russian Roulette"] = FormatFlag(s_Settings.russianRoulette);
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

    void ReadMaterial(const EditorSettings::Values& values, PathTracer::Material& material)
    {
        const auto text = values.find("Material");
        if (text == values.end())
            return;
        for (int index = 0; index < IM_ARRAYSIZE(kMaterialNames); ++index)
        {
            if (text->second == kMaterialNames[index])
                material = (PathTracer::Material)index;
        }
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
        ReadMaterial(values, s_Settings.material);
        ReadUInt(values, "Samples per Frame", s_Settings.samplesPerFrame);
        ReadUInt(values, "Target Samples", s_Settings.targetSamples);
        ReadUInt(values, "Seed", s_Settings.seed);
        ReadUInt(values, "Max Bounces", s_Settings.maxBounces);
        ReadFlag(values, "Russian Roulette", s_Settings.russianRoulette);
        ReadFlag(values, "Geometric Normals", s_Settings.geometricNormals);
        ReadFlag(values, "Furnace", s_Settings.furnace);
        ReadFloat(values, "Furnace Albedo", s_Settings.furnaceAlbedo);
        ReadFloat(values, "Furnace Emission", s_Settings.furnaceEmission);
        ClampSettings();
    }

    // 측정 조건의 줄에 담는 path tracer의 설정과 sample 수. 설정은 PathTracer.cpp의 HaveSameImage가 견주는 것, 곧 image를 정하는 것만
    // 담는다. 값은 editor.ini와 같은 형식으로 쓴다.
    void DescribeConditions(MeasurementConditions::Values& values)
    {
        values["pt material"] = kMaterialNames[(int)s_Settings.material];
        values["pt seed"] = std::to_string(s_Settings.seed);
        values["pt max bounces"] = std::to_string(s_Settings.maxBounces);
        values["pt russian roulette"] = FormatFlag(s_Settings.russianRoulette);
        values["pt geometric normals"] = FormatFlag(s_Settings.geometricNormals);
        values["pt furnace"] = FormatFlag(s_Settings.furnace);
        values["pt furnace albedo"] = FormatFloat(s_Settings.furnaceAlbedo);
        values["pt furnace emission"] = FormatFloat(s_Settings.furnaceEmission);
        values["pt samples"] = std::to_string(PathTracer::GetProgress().samples);
    }

    bool ReadConditionFlag(const std::string& text, bool& value)
    {
        value = text == "1";
        return text == "0" || text == "1";
    }

    // 줄의 image를 다시 만들도록 pt samples를 Target Samples로 두고 처음부터 다시 쌓는다. 0이면 path tracer를 쌓지 않은 때의 줄이라
    // Target Samples를 그대로 둔다. ClampSettings가 바꿀 값은 받지 않는다. /fp:fast에서는 NaN과의 비교가 참일 수 있어 유한한지 따로 본다.
    void ReadConditions(const MeasurementConditions::Values& values, MeasurementConditions::Reading& reading)
    {
        using MeasurementConditions::ReadFloat;
        using MeasurementConditions::ReadUInt;
        using MeasurementConditions::ReadValue;

        PathTracer::Settings settings = s_Settings;
        uint32_t samples = 0;
        ReadValue(values, "pt material", reading, [&settings](const std::string& text)
        {
            const auto name = std::find(std::begin(kMaterialNames), std::end(kMaterialNames), text);
            settings.material = (PathTracer::Material)(name - std::begin(kMaterialNames));
            return name != std::end(kMaterialNames);
        });
        ReadValue(values, "pt seed", reading, [&settings](const std::string& text) { return ReadUInt(text, settings.seed); });
        ReadValue(values, "pt max bounces", reading, [&settings](const std::string& text)
        {
            return ReadUInt(text, settings.maxBounces) && settings.maxBounces <= kMaxBounces;
        });
        ReadValue(values, "pt russian roulette", reading, [&settings](const std::string& text) { return ReadConditionFlag(text, settings.russianRoulette); });
        ReadValue(values, "pt geometric normals", reading, [&settings](const std::string& text) { return ReadConditionFlag(text, settings.geometricNormals); });
        ReadValue(values, "pt furnace", reading, [&settings](const std::string& text) { return ReadConditionFlag(text, settings.furnace); });
        ReadValue(values, "pt furnace albedo", reading, [&settings](const std::string& text)
        {
            return ReadFloat(text, settings.furnaceAlbedo) && std::isfinite(settings.furnaceAlbedo) && settings.furnaceAlbedo >= 0.0f &&
                settings.furnaceAlbedo <= 1.0f;
        });
        ReadValue(values, "pt furnace emission", reading, [&settings](const std::string& text)
        {
            return ReadFloat(text, settings.furnaceEmission) && std::isfinite(settings.furnaceEmission) && settings.furnaceEmission >= 0.0f;
        });
        ReadValue(values, "pt samples", reading, [&samples](const std::string& text) { return ReadUInt(text, samples) && samples <= kMaxTargetSamples; });
        reading.applies.push_back([settings, samples]
        {
            s_Settings = settings;
            if (samples > 0)
                s_Settings.targetSamples = samples;
            PathTracer::Reset();
        });
    }

    void ShowConditions(void)
    {
        ImGui::SeparatorText("Conditions");
        const std::string& unavailable = MeasurementConditions::GetUnavailableReason();
        ImGui::BeginDisabled(!unavailable.empty());
        if (ImGui::Button("Log"))
        {
            const std::string line = kLogPrefix + MeasurementConditions::Format(MeasurementConditions::Describe());
            Utility::Print((line + "\n").c_str());
            ImGui::SetClipboardText(line.c_str());
            s_RestoreField.SetLine(line);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", unavailable.empty() ?
            "Prints the camera, resolution, lights, settings above and samples as one line to the Output window, copies it and fills in "
            "the line below. Restoring it sets the Target Samples to those samples and starts over, so the image comes out the same." :
            unavailable.c_str());
        s_RestoreField.Show();
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
    MeasurementConditions::Register("pt", { DescribeConditions, ReadConditions, { std::string("settings.") + kSettingsSection } });
    RegisterCommands();
    ControlChannel::RegisterState("pt", DescribeProgress);
}

void PathTracerPanel::Shutdown(void)
{
    s_SampleWaits.clear();
    MeasurementConditions::Unregister("pt");
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
        ShowConditions();
    });
}

void PathTracerPanel::Render(ComputeContext& context, const Math::Camera& camera, const D3D12_VIEWPORT& rasterViewport,
    const RaytracingScene& scene, const Math::Vector3& lightDirection, const Math::Vector3& lightIlluminance, const Math::Vector3& skyRadiance,
    ColorBuffer& sceneColor)
{
    if (s_Show)
        PathTracer::Render(context, s_Settings, camera, rasterViewport, scene, lightDirection, lightIlluminance, skyRadiance, sceneColor);
    PathTracer::Capture(context);
}
