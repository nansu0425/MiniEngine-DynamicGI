#include "Core/pch.h"
#include "Core/Automation/D3D12MessageLog.h"
#include "Core/Display/Display.h"

#include <map>
#include <mutex>

namespace
{
    const char* const kSeverityNames[] = { "CORRUPTION", "ERROR", "WARNING", "INFO", "MESSAGE" };
    const char* const kCategoryNames[] = { "APPLICATION_DEFINED", "MISCELLANEOUS", "INITIALIZATION", "CLEANUP", "COMPILATION",
        "STATE_CREATION", "STATE_SETTING", "STATE_GETTING", "RESOURCE_MANIPULATION", "EXECUTION", "SHADER" };

    // callback은 main thread가 아닌 thread에서도 오므로 아래 상태는 이 mutex 아래에서만 쓴다.
    std::mutex s_Mutex;
    D3D12MessageLog::Status s_Status;
    DWORD s_CallbackCookie = 0;
    // 종료 report는 살아 남은 object를 하나도 빠뜨리지 않고 이름까지 남기려고 ID마다의 제한을 두지 않는다.
    bool s_ReportingLiveObjects = false;
    std::map<int, uint64_t> s_CountById;

    template <size_t N>
    const char* FindName(const char* const (&names)[N], int value)
    {
        return value >= 0 && value < (int)N ? names[value] : "UNKNOWN";
    }

    std::string FormatMessageLine(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* description)
    {
        // frame은 다른 thread에서 읽기도 하므로 근사다.
        char head[128];
        sprintf_s(head, "D3D12 %s #%d %s frame=%llu: ", FindName(kSeverityNames, severity), (int)id, FindName(kCategoryNames, category),
            (unsigned long long)Graphics::GetFrameCount());

        std::string line = head;
        line += description;
        for (size_t i = strlen(head); i < line.size(); ++i)
        {
            if (line[i] == '\r' || line[i] == '\n' || line[i] == '\t')
                line[i] = ' ';
        }
        return line + "\n";
    }

    struct Message
    {
        D3D12_MESSAGE_CATEGORY category;
        D3D12_MESSAGE_SEVERITY severity;
        D3D12_MESSAGE_ID id;
        std::string description;
    };
    // 상태 줄보다 먼저 쓰지 않으려고, Start가 상태 줄을 쓰기 전에 callback으로 온 message를 모아 둔다.
    std::vector<Message> s_MessagesBeforeStatus;

    // s_Mutex를 쥐고 부른다.
    void LogMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* description)
    {
        if ((int)severity >= 0 && (int)severity < (int)_countof(s_Status.countBySeverity))
            ++s_Status.countBySeverity[severity];

        if (!s_ReportingLiveObjects)
        {
            const uint64_t count = ++s_CountById[id];
            if (count == D3D12MessageLog::kLoggedPerId + 1)
            {
                char line[96];
                sprintf_s(line, "D3D12 #%d: logged %llu times, the rest are only counted\n", (int)id, (unsigned long long)D3D12MessageLog::kLoggedPerId);
                Utility::PrintToLogFile(line);
            }
            if (count > D3D12MessageLog::kLoggedPerId)
                return;
        }
        Utility::PrintToLogFile(FormatMessageLine(category, severity, id, description).c_str());
    }

    void __stdcall OnMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void*)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_Status.logged)
            LogMessage(category, severity, id, description);
        else
            s_MessagesBeforeStatus.push_back({ category, severity, id, description });
    }

    std::vector<Message> ReadStoredMessages(ID3D12InfoQueue* infoQueue)
    {
        std::vector<Message> messages;
        const UINT64 count = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T size = 0;
            if (FAILED(infoQueue->GetMessage(index, nullptr, &size)))
                continue;
            std::vector<char> bytes(size);
            D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            if (SUCCEEDED(infoQueue->GetMessage(index, message, &size)))
                messages.push_back({ message->Category, message->Severity, message->ID, message->pDescription });
        }
        return messages;
    }

    void ReportNotLogged(const std::string& reason)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Status.notLoggedReason = reason;
        Utility::PrintToLogFile(("D3D12 messages: not logged (" + reason + ")\n").c_str());
    }
}

void D3D12MessageLog::Start(ID3D12Device* device, bool gpuBasedValidation)
{
    // 종료 report가 보여 주는 device의 Refcount를 바꾸지 않게 info queue는 쓰자마자 놓는다.
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        ReportNotLogged("debug layer off");
        return;
    }
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue1;
    if (FAILED(infoQueue.As(&infoQueue1)))
    {
        ReportNotLogged("ID3D12InfoQueue1 unavailable");
        return;
    }
    // 등록한 뒤의 message가 쌓인 것과 겹쳐 두 번 쓰이지 않게 등록하기 전에 꺼낸다.
    std::vector<Message> messages = ReadStoredMessages(infoQueue.Get());
    // FLAG_NONE이라 debugger 출력처럼 storage filter를 지난 message만 온다.
    if (FAILED(infoQueue1->RegisterMessageCallback(OnMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &s_CallbackCookie)))
    {
        ReportNotLogged("RegisterMessageCallback failed");
        return;
    }

    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Status.logged = true;
    s_Status.gpuBasedValidation = gpuBasedValidation;
    Utility::PrintToLogFile(gpuBasedValidation ? "D3D12 messages: logged, GPU-based validation on\n" : "D3D12 messages: logged\n");
    messages.insert(messages.end(), s_MessagesBeforeStatus.begin(), s_MessagesBeforeStatus.end());
    s_MessagesBeforeStatus.clear();
    for (const Message& message : messages)
        LogMessage(message.category, message.severity, message.id, message.description.c_str());
}

void D3D12MessageLog::BeginLiveObjectReport(void)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (!s_Status.logged)
        return;
    s_ReportingLiveObjects = true;
    Utility::PrintToLogFile("D3D12 live objects at exit:\n");
}

void D3D12MessageLog::Stop(ID3D12Device* device)
{
    if (!GetStatus().logged)
        return;

    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        infoQueue->UnregisterMessageCallback(s_CallbackCookie);

    std::lock_guard<std::mutex> lock(s_Mutex);
    for (const auto& idCount : s_CountById)
    {
        if (idCount.second <= kLoggedPerId)
            continue;
        char line[64];
        sprintf_s(line, "D3D12 #%d total=%llu\n", idCount.first, (unsigned long long)idCount.second);
        Utility::PrintToLogFile(line);
    }
}

D3D12MessageLog::Status D3D12MessageLog::GetStatus(void)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    return s_Status;
}

const char* D3D12MessageLog::GetSeverityName(D3D12_MESSAGE_SEVERITY severity)
{
    return FindName(kSeverityNames, severity);
}
