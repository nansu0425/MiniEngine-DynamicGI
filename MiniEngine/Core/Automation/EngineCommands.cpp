#include "Core/pch.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Automation/D3D12MessageLog.h"
#include "Core/Automation/FrameCapture.h"
#include "Core/Display/Display.h"
#include "Core/Editor/EditorSettings.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Profiling/EngineProfiling.h"
#include "Core/UI/ImGuiLayer.h"

#include <algorithm>
#include <cmath>

namespace GameCore
{
    extern HWND g_hWnd;
}

namespace Graphics
{
    extern bool g_bHDRDisplay;
}

using namespace ControlChannel;

namespace
{
#if defined(_DEBUG)
    const char* kConfiguration = "Debug";
#elif defined(RELEASE)
    const char* kConfiguration = "Release";
#else
    const char* kConfiguration = "Profile";
#endif

    // UI 배치를 바꾸면 ImGui의 docking이 scene 영역을 다시 정하고 rendering buffer가 그 크기를 따라갈 때까지 걸리는 frame 수
    constexpr uint32_t kFramesToSettleLayout = 2;

    // Window section을 적용하면 창을 숨긴 채로 배치만 바꾸므로 읽기만 한다. 창의 크기는 window 명령으로 바꾼다.
    const char* kReadOnlySection = "Window";

    std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(' ');
        if (first == std::string::npos)
            return std::string();
        return text.substr(first, text.find_last_not_of(' ') - first + 1);
    }

    bool StartsWith(const std::string& text, const std::string& prefix)
    {
        return text.compare(0, prefix.size(), prefix) == 0;
    }

    std::string FormatSize(uint32_t width, uint32_t height)
    {
        char text[32];
        sprintf_s(text, "%ux%u", width, height);
        return text;
    }

    std::string JoinNames(const std::vector<std::string>& names)
    {
        std::string joined;
        for (const std::string& name : names)
            joined += (joined.empty() ? "" : ", ") + name;
        return joined;
    }

    std::string FindGpuName(void)
    {
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapterByLuid(Graphics::g_Device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) ||
            FAILED(adapter->GetDesc1(&desc)))
            return "unknown";
        return Utility::ToUTF8(desc.Description);
    }

    // state를 읽을 때마다 DXGI factory를 만들지 않으려고 한 번만 찾는다.
    const std::string& GetGpuName(void)
    {
        static const std::string name = FindGpuName();
        return name;
    }

    std::string GetExePath(void)
    {
        // MAX_PATH보다 긴 경로도 담도록 Windows 경로의 최대 길이로 잡는다.
        std::wstring path(32768, L'\0');
        path.resize(GetModuleFileNameW(nullptr, &path[0], (DWORD)path.size()));
        return Utility::ToUTF8(path);
    }

    RECT GetClientSize(void)
    {
        RECT client = {};
        GetClientRect(GameCore::g_hWnd, &client);
        return client;
    }

    Fields DescribeD3D12Messages(void)
    {
        const D3D12MessageLog::Status status = D3D12MessageLog::GetStatus();
        if (!status.logged)
            return Fields().Flag("logged", false).Text("reason", status.notLoggedReason);

        Fields fields;
        fields.Flag("logged", true).Flag("gpu_based_validation", status.gpuBasedValidation);
        for (int severity = 0; severity < (int)_countof(status.countBySeverity); ++severity)
            fields.Integer(Utility::ToLower(D3D12MessageLog::GetSeverityName((D3D12_MESSAGE_SEVERITY)severity)), (int64_t)status.countBySeverity[severity]);
        return fields;
    }

    Fields DescribeUI(void)
    {
        const RECT client = GetClientSize();
        const RECT scene = ImGuiLayer::GetSceneViewport();
        EditorSettings::Values panels;
        EditorSettings::ReadSection("Panels", panels);
        Fields open;
        for (const auto& panel : panels)
            open.Flag(panel.first, panel.second == "1");
        return Fields()
            .Flag("visible", ImGuiLayer::IsVisible())
            .Object("client", Fields().Integer("width", client.right).Integer("height", client.bottom))
            .Object("scene", Fields()
                .Integer("left", scene.left)
                .Integer("top", scene.top)
                .Integer("width", Graphics::g_SceneColorBuffer.GetWidth())
                .Integer("height", Graphics::g_SceneColorBuffer.GetHeight()))
            .Object("panels", open);
    }

    Fields DescribeSection(const EditorSettings::Values& values)
    {
        Fields fields;
        for (const auto& value : values)
            fields.Text(value.first, value.second);
        return fields;
    }

    Fields DescribeSettings(void)
    {
        Fields sections;
        for (const std::string& section : EditorSettings::GetSectionNames())
        {
            EditorSettings::Values values;
            EditorSettings::ReadSection(section, values);
            sections.Object(section, DescribeSection(values));
        }
        return sections;
    }

    Fields DescribeEngine(void)
    {
        return Fields()
            .Text("config", kConfiguration)
            .Text("gpu", GetGpuName())
            .Text("exe", GetExePath())
            .Flag("hdr_output", Graphics::g_bEnableHDROutput)
            .Flag("hdr_display", Graphics::g_bHDRDisplay)
            .Object("d3d12_messages", DescribeD3D12Messages());
    }

    void ReplyState(const std::string& path, const Reply& reply)
    {
        Fields state;
        std::string error;
        if (!GetState(path, state, error))
        {
            reply.Error(error);
            return;
        }
        reply.Ok(Fields().Object("state", state));
    }

    void ReplyStatus(const std::string&, const Reply& reply)
    {
        const RECT client = GetClientSize();
        EditorSettings::Values tuning;
        EditorSettings::ReadSection("Engine Tuning", tuning);
        reply.Ok(Fields()
            .Integer("frame", Graphics::GetFrameCount())
            .Text("client", FormatSize(client.right, client.bottom))
            .Text("scene", FormatSize(Graphics::g_SceneColorBuffer.GetWidth(), Graphics::g_SceneColorBuffer.GetHeight()))
            .Text("ui", ImGuiLayer::IsVisible() ? "shown" : "hidden")
            .Flag("vsync", tuning["Timing/VSync"] == "on")
            .Flag("hdr_output", Graphics::g_bEnableHDROutput)
            .Flag("hdr_display", Graphics::g_bHDRDisplay)
            .Text("gpu", GetGpuName())
            .Text("config", kConfiguration)
            .Text("exe", GetExePath())
            .Object("d3d12_messages", DescribeD3D12Messages())
            .Object("app", ControlChannel::GetStatus()));
    }

    void WaitFrames(const std::string& arguments, const Reply& reply)
    {
        uint32_t frames = 0;
        if (sscanf_s(arguments.c_str(), "%u", &frames) != 1)
        {
            reply.Error("usage: frames <n>");
            return;
        }
        AfterFrames(frames, [reply] { reply.Ok(Fields().Integer("frame", Graphics::GetFrameCount())); });
    }

    void Quit(const std::string&, const Reply& reply)
    {
        reply.Ok();
        // 사용자가 창을 닫을 때와 같은 길로 끝내 editor 상태를 저장한다.
        PostMessage(GameCore::g_hWnd, WM_CLOSE, 0, 0);
    }

    bool ReadExistingSection(const std::string& section, EditorSettings::Values& values, const Reply& reply)
    {
        if (!EditorSettings::ReadSection(section, values))
        {
            reply.Error("unknown section '" + section + "'. Sections: " + JoinNames(EditorSettings::GetSectionNames()));
            return false;
        }
        return true;
    }

    // section은 형식이 틀린 값을 조용히 버리거나 다른 값으로 읽으므로, 적용한 뒤 다시 읽은 값이 요청과 같은지 본다.
    // 숫자는 float로 비교하되, ExpVar처럼 log2로 저장했다가 되돌리며 생기는 몇 ulp의 차이는 같은 값으로 본다.
    bool IsAppliedAsRequested(const std::string& requested, const std::string& applied)
    {
        char* requestedEnd = nullptr;
        char* appliedEnd = nullptr;
        const float requestedNumber = strtof(requested.c_str(), &requestedEnd);
        const float appliedNumber = strtof(applied.c_str(), &appliedEnd);
        const bool bothNumbers = requestedEnd != requested.c_str() && *requestedEnd == '\0' && appliedEnd != applied.c_str() && *appliedEnd == '\0';
        if (!bothNumbers)
            return requested == applied;
        return std::fabs(requestedNumber - appliedNumber) <= 1e-6f * std::max(std::fabs(requestedNumber), std::fabs(appliedNumber));
    }

    void SetSettings(const std::string& arguments, const Reply& reply)
    {
        const size_t sectionEnd = arguments.find(':');
        if (sectionEnd == std::string::npos)
        {
            reply.Error("usage: settings set <section>: <key>=<value>; <key>=<value>");
            return;
        }

        const std::string section = Trim(arguments.substr(0, sectionEnd));
        if (section == kReadOnlySection)
        {
            reply.Error("the Window section cannot be set while running because applying it hides the window. Use window client <W>x<H>.");
            return;
        }
        EditorSettings::Values values;
        if (!ReadExistingSection(section, values, reply))
            return;
        const EditorSettings::Values original = values;

        std::vector<std::pair<std::string, std::string>> requests;
        const std::string assignments = arguments.substr(sectionEnd + 1);
        for (size_t start = 0; start < assignments.size();)
        {
            const size_t end = std::min(assignments.find("; ", start), assignments.size());
            const std::string assignment = assignments.substr(start, end - start);
            start = end + 2;
            if (Trim(assignment).empty())
                continue;

            const size_t separator = assignment.find('=');
            const std::string key = Trim(assignment.substr(0, separator));
            if (separator == std::string::npos || values.find(key) == values.end())
            {
                reply.Error("unknown key '" + key + "' in " + section + ". Nothing was set.");
                return;
            }
            const std::string value = Trim(assignment.substr(separator + 1));
            values[key] = value;
            requests.emplace_back(key, value);
        }

        EditorSettings::ApplySection(section, values);
        EditorSettings::ReadSection(section, values);
        Fields applied, rejected;
        bool anyRejected = false;
        for (const auto& request : requests)
        {
            applied.Text(request.first, values[request.first]);
            if (!IsAppliedAsRequested(request.second, values[request.first]))
            {
                rejected.Text(request.first, values[request.first]);
                anyRejected = true;
            }
        }

        if (anyRejected)
        {
            // BoolVar처럼 틀린 값을 다른 값으로 읽은 경우도 있으므로 요청 전의 값으로 되돌린다.
            EditorSettings::ApplySection(section, original);
            reply.Error("the section did not take some values, so nothing was set. rejected has what they were read back as.",
                Fields().Object("rejected", rejected));
            return;
        }
        reply.Ok(Fields().Object("values", applied).Object("state", GetState("settings." + section)));
    }

    void GetSettings(const std::string& section, const Reply& reply)
    {
        EditorSettings::Values values;
        if (!ReadExistingSection(section, values, reply))
            return;
        reply.Ok(Fields().Object("values", DescribeSection(values)));
    }

    void HandleSettings(const std::string& arguments, const Reply& reply)
    {
        if (StartsWith(arguments, "set "))
            SetSettings(arguments.substr(4), reply);
        else if (arguments == "get" || StartsWith(arguments, "get "))
            GetSettings(arguments.size() > 4 ? Trim(arguments.substr(4)) : std::string(), reply);
        else
            reply.Error("usage: settings get <section> | settings set <section>: <key>=<value>; <key>=<value>");
    }

    void SetUIVisible(const std::string& arguments, const Reply& reply)
    {
        if (arguments != "show" && arguments != "hide")
        {
            reply.Error("usage: ui show|hide");
            return;
        }
        ImGuiLayer::SetVisible(arguments == "show");
        AfterFrames(kFramesToSettleLayout, [reply] { reply.Ok(Fields().Object("state", GetState("ui"))); });
    }

    void SetPanelOpen(const std::string& arguments, const Reply& reply)
    {
        const size_t actionStart = arguments.rfind(' ');
        const std::string action = actionStart == std::string::npos ? std::string() : arguments.substr(actionStart + 1);
        if (action != "open" && action != "close")
        {
            reply.Error("usage: panel <name> open|close");
            return;
        }

        const std::string name = Trim(arguments.substr(0, actionStart));
        EditorSettings::Values panels;
        EditorSettings::ReadSection("Panels", panels);
        if (panels.find(name) == panels.end())
        {
            std::vector<std::string> names;
            for (const auto& panel : panels)
                names.push_back(panel.first);
            reply.Error("unknown panel '" + name + "'. Panels: " + JoinNames(names));
            return;
        }

        panels[name] = action == "open" ? "1" : "0";
        EditorSettings::ApplySection("Panels", panels);
        AfterFrames(kFramesToSettleLayout, [reply] { reply.Ok(Fields().Object("state", GetState("ui"))); });
    }

    // 예전 Log 줄과 같은 해상도로 재려고 client 크기를 맞춘다. 실제 창 크기 변경과 같은 WM_SIZE 경로를 지난다.
    void ResizeWindow(const std::string& arguments, const Reply& reply)
    {
        uint32_t width = 0, height = 0;
        if (sscanf_s(arguments.c_str(), "client %ux%u", &width, &height) != 2 || width == 0 || height == 0)
        {
            reply.Error("usage: window client <W>x<H>");
            return;
        }

        const HWND window = GameCore::g_hWnd;
        if (IsZoomed(window) || IsIconic(window))
            ShowWindow(window, SW_RESTORE);
        RECT rect = { 0, 0, (LONG)width, (LONG)height };
        AdjustWindowRectExForDpi(&rect, (DWORD)GetWindowLongPtr(window, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE), GetDpiForWindow(window));
        SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        AfterFrames(kFramesToSettleLayout, [reply, width, height]
        {
            const RECT client = GetClientSize();
            if ((uint32_t)client.right != width || (uint32_t)client.bottom != height)
                reply.Error("the client is " + FormatSize(client.right, client.bottom) + ". The screen may be too small.");
            else
                reply.Ok(Fields().Text("client", FormatSize(width, height)).Object("state", GetState("ui")));
        });
    }

    void CaptureFrame(const std::string& arguments, const Reply& reply)
    {
        const size_t targetEnd = arguments.find(' ');
        const std::string path = targetEnd == std::string::npos ? std::string() : Trim(arguments.substr(targetEnd + 1));
        FrameCapture::Target target;
        if (!FrameCapture::FindTarget(arguments.substr(0, targetEnd), target) || path.empty())
        {
            reply.Error("usage: capture scene|overlay|display <path without extension>");
            return;
        }
        FrameCapture::Request(target, path, reply);
    }

    // profile 명령이 frame마다 모으는 scope의 GPU time
    struct GpuTimeMeasurement
    {
        std::wstring scope;
        uint32_t frames;
        uint32_t measured;
        double sum;
        float minimum;
        float maximum;
        Reply reply;
    };

    // Profiler는 frame을 시작할 때 시간을 읽어 들이고 명령은 그 뒤에 실행되므로, frame마다 한 번 읽으면 frame마다 새 값이다.
    void MeasureNextFrame(const std::shared_ptr<GpuTimeMeasurement>& measurement)
    {
        AfterFrames(1, [measurement]
        {
            GpuTimeMeasurement& m = *measurement;
            const std::string scope = Utility::ToUTF8(m.scope);
            float cpu = 0.0f, gpu = 0.0f;
            if (EngineProfiling::IsPaused())
            {
                m.reply.Error("the profiler was paused while measuring, so its times stopped changing");
                return;
            }
            // 지난 frame에 scope를 지나지 않았으면 CPU time이 0이고, GPU time은 그 timer가 마지막으로 잰 값에 머문다.
            if (!EngineProfiling::FindLastTimes(m.scope, cpu, gpu) || cpu <= 0.0f)
            {
                m.reply.Error("'" + scope + "' did not run in a frame it was measured in");
                return;
            }

            m.sum += gpu;
            m.minimum = m.measured == 0 ? gpu : std::min(m.minimum, gpu);
            m.maximum = m.measured == 0 ? gpu : std::max(m.maximum, gpu);
            if (++m.measured < m.frames)
            {
                MeasureNextFrame(measurement);
                return;
            }
            m.reply.Ok(Fields()
                .Text("scope", scope)
                .Integer("frames", m.measured)
                .Object("gpu_ms", Fields().Float("mean", (float)(m.sum / m.measured)).Float("min", m.minimum).Float("max", m.maximum)));
        });
    }

    void MeasureGpuTime(const std::string& arguments, const Reply& reply)
    {
        const size_t framesStart = arguments.rfind(" frames=");
        const std::string scope = framesStart == std::string::npos ? std::string() : Trim(arguments.substr(0, framesStart));
        uint32_t frames = 0;
        if (scope.empty() || sscanf_s(arguments.c_str() + framesStart, " frames=%u", &frames) != 1 || frames == 0)
        {
            reply.Error("usage: profile <scope> frames=<n>");
            return;
        }
        if (EngineProfiling::IsPaused())
        {
            reply.Error("the profiler is paused, so its times do not change");
            return;
        }
        float cpu = 0.0f, gpu = 0.0f;
        if (!EngineProfiling::FindLastTimes(Utility::ToWide(scope), cpu, gpu))
        {
            reply.Error("no scope '" + scope + "' in the profiler");
            return;
        }
        MeasureNextFrame(std::make_shared<GpuTimeMeasurement>(GpuTimeMeasurement{ Utility::ToWide(scope), frames, 0, 0.0, 0.0f, 0.0f, reply }));
    }

    // harness가 log에서 명령 사이의 구간을 찾게 표시를 남긴다.
    void MarkLog(const std::string& arguments, const Reply& reply)
    {
        if (!StartsWith(arguments, "mark "))
        {
            reply.Error("usage: log mark <text>");
            return;
        }
        Utility::Print(("[Harness] mark " + arguments.substr(5) + "\n").c_str());
        reply.Ok();
    }

    void InjectFault(const std::string& fault)
    {
        if (fault == "assert")
        {
            ASSERT(fault != "assert", "fault injected through the control channel");
        }
        else if (fault == "exception")
        {
            *static_cast<volatile int*>(nullptr) = 0;
        }
        else if (fault == "hang")
        {
            for (;;)
                Sleep(1000);
        }
        else if (fault == "d3d12_error")
        {
            // 크기가 0인 buffer는 debug layer가 ERROR를 내고 E_INVALIDARG로 거절할 뿐 device는 그대로다.
            const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
            const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(0);
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            (void)Graphics::g_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(&resource));
        }
    }

    // harness가 crash, hang, D3D12 message를 판정하는지 확인하려고 장애를 일부러 낸다.
    void HandleFault(const std::string& arguments, const Reply& reply)
    {
        if (arguments != "assert" && arguments != "exception" && arguments != "hang" && arguments != "d3d12_error")
        {
            reply.Error("usage: fault assert|exception|hang|d3d12_error");
            return;
        }
#ifdef RELEASE
        if (arguments == "assert")
        {
            reply.Error("ASSERT is compiled out in Release");
            return;
        }
#endif
        const D3D12MessageLog::Status d3d12Messages = D3D12MessageLog::GetStatus();
        if (arguments == "d3d12_error" && !d3d12Messages.logged)
        {
            reply.Error("D3D12 messages are not logged (" + d3d12Messages.notLoggedReason + ")");
            return;
        }
        reply.Ok();
        // 응답이 harness에 닿을 시간을 두고 다음 frame에 일으킨다.
        AfterFrames(1, [arguments] { InjectFault(arguments); });
    }
}

// ControlChannel::Initialize가 부른다.
namespace ControlChannel
{
    void RegisterEngineCommands(void)
    {
        RegisterState("ui", DescribeUI);
        RegisterState("settings", DescribeSettings);
        RegisterState("engine", DescribeEngine);
        Register("state", "state | state <path>: the scene, editor and engine state that harness checks are judged by. "
            "<path> joins keys with '.', as in camera or settings.Engine Tuning, and keeps only that part. "
            "Positions are in world units. Camera angles are in radians: heading turns about world up (+y), 0 looking toward -z and growing toward -x, "
            "and pitch is up from the horizon. Sun elevation and azimuth are in degrees as sun set takes them. "
            "aspect is width over height. Sizes and left, top are in client pixels from the top left. "
            "settings has every section as settings get reads it. Commands that change the state reply with the changed part as state", ReplyState);
        Register("status", "status", ReplyStatus);
        Register("frames", "frames <n>: replies after n more frames", WaitFrames);
        Register("quit", "quit: exits as if the window were closed, saving the editor state", Quit);
        Register("settings", "settings get <section> | settings set <section>: <key>=<value>; <key>=<value>  (sections and values as in editor.ini). "
            "set changes nothing and fails if a value does not read back as given", HandleSettings);
        Register("ui", "ui show|hide: same as F1. Hidden, the scene fills the client", SetUIVisible);
        Register("panel", "panel <name> open|close: same as the View menu", SetPanelOpen);
        Register("window", "window client <W>x<H>", ResizeWindow);
        Register("capture", "capture scene|overlay|display <path without extension>: scene is the HDR before post effects as float32 .dds, "
            "overlay is the debug view layer as .png with premultiplied alpha, display is the presented image with UI as .png", CaptureFrame);
        Register("profile", "profile <scope> frames=<n>: the GPU time in ms of the Profiler scope named <scope> (as in its Scope column) "
            "over the next n frames, as mean, min and max. Fails if the scope does not run in one of those frames. "
            "Release compiles the timers out", MeasureGpuTime);
        Register("log", "log mark <text>", MarkLog);
        Register("fault", "fault assert|exception|hang|d3d12_error: injects a failure in the next frame to test the harness. "
            "d3d12_error makes a D3D12 call that the debug layer rejects with an ERROR, and is refused when D3D12 messages are not logged", HandleFault);
    }
}
