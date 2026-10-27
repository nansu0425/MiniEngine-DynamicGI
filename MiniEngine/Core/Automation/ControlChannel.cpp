#include "Core/pch.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Display/Display.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace ControlChannel
{
    struct Request
    {
        std::string line;
        std::string response;
        bool answered = false;
        // 응답을 기다리던 client가 연결을 끊었다. main thread는 이 명령의 응답을 더 기다리지 않는다.
        bool abandoned = false;
    };

    // engine 기능의 명령. EngineCommands.cpp에 있다.
    void RegisterEngineCommands(void);
}

using namespace ControlChannel;

namespace
{
    struct Command
    {
        std::string usage;
        Handler handler;
    };

    struct ScheduledAction
    {
        uint64_t frame;
        std::function<void()> action;
    };

    struct Connection
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    constexpr DWORD kPipeBufferSize = 64 * 1024;
    // client가 응답을 읽지 않아도 app이 끝날 수 있게 응답 하나를 쓰는 데 기다리는 시간
    constexpr DWORD kWriteTimeoutMs = 1000;
    // 응답을 기다리는 동안 client가 연결을 끊었는지 보는 간격
    constexpr std::chrono::milliseconds kDisconnectCheckInterval(100);

    std::map<std::string, Command> s_Commands;
    std::map<std::string, std::string> s_Status;
    std::map<std::string, std::function<Fields(void)>> s_StateGroups;
    std::vector<ScheduledAction> s_ScheduledActions;

    // 아래는 pipe thread와 main thread가 같이 쓴다.
    std::mutex s_Mutex;
    std::condition_variable s_Answered;
    std::deque<std::shared_ptr<Request>> s_Queue;
    std::shared_ptr<Request> s_Executing;
    bool s_Stopping = false;
    std::vector<Connection> s_Connections;

    std::atomic<uint64_t> s_FrameCount{ 0 };
    std::wstring s_PipePath;
    HANDLE s_StopEvent = nullptr;
    std::thread s_ListenThread;

    std::string Quote(const std::string& text)
    {
        std::string quoted = "\"";
        for (const char c : text)
        {
            switch (c)
            {
            case '"': quoted += "\\\""; break;
            case '\\': quoted += "\\\\"; break;
            case '\n': quoted += "\\n"; break;
            case '\r': quoted += "\\r"; break;
            case '\t': quoted += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20)
                {
                    char escaped[8];
                    sprintf_s(escaped, "\\u%04x", (unsigned char)c);
                    quoted += escaped;
                }
                else
                    quoted += c;
            }
        }
        return quoted + "\"";
    }

    std::string MakeError(const std::string& message, const Fields& fields = Fields())
    {
        const std::string members = fields.ToMembers();
        return "{\"ok\":false,\"error\":" + Quote(message) + (members.empty() ? "" : "," + members) + "}";
    }

    bool IsClientConnected(HANDLE pipe)
    {
        DWORD available = 0;
        return PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    }

    void Answer(Request& request, const std::string& response)
    {
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            if (request.answered)
                return;
            request.response = response;
            request.answered = true;
        }
        s_Answered.notify_all();
    }

    // 명령을 main thread에 넘기고 응답을 기다린다. client가 기다리다 떠나면 빈 문자열을 돌려준다.
    // 그 명령을 버려서, client가 포기한 긴 명령이 다음 client의 명령을 막지 않게 한다.
    std::string AskMainThread(HANDLE pipe, const std::string& line)
    {
        const auto request = std::make_shared<Request>();
        request->line = line;

        std::unique_lock<std::mutex> lock(s_Mutex);
        s_Queue.push_back(request);
        while (!s_Answered.wait_for(lock, kDisconnectCheckInterval, [&request] { return request->answered || s_Stopping; }))
        {
            if (!IsClientConnected(pipe))
            {
                request->abandoned = true;
                return std::string();
            }
        }
        return request->answered ? request->response : MakeError("the app is exiting");
    }

    // I/O가 끝나면 true를, 연결이 끊기거나 app이 끝나면 false를 돌려준다.
    bool WaitForIo(HANDLE pipe, OVERLAPPED& overlapped, DWORD& transferred)
    {
        const HANDLE events[] = { overlapped.hEvent, s_StopEvent };
        if (WaitForMultipleObjects(_countof(events), events, FALSE, INFINITE) != WAIT_OBJECT_0)
        {
            CancelIo(pipe);
            GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
            return false;
        }
        return GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
    }

    bool ReadLine(HANDLE pipe, HANDLE event, std::string& pending, std::string& line)
    {
        for (;;)
        {
            const size_t end = pending.find('\n');
            if (end != std::string::npos)
            {
                line = pending.substr(0, end);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                pending.erase(0, end + 1);
                return true;
            }

            char buffer[4096];
            OVERLAPPED overlapped = {};
            overlapped.hEvent = event;
            DWORD read = 0;
            if (!ReadFile(pipe, buffer, sizeof(buffer), nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING)
                return false;
            if (!WaitForIo(pipe, overlapped, read) || read == 0)
                return false;
            pending.append(buffer, read);
        }
    }

    // app이 끝나는 중에도 쓴다. quit의 응답이 그렇다.
    bool WriteLine(HANDLE pipe, HANDLE event, const std::string& text)
    {
        const std::string line = text + "\n";
        OVERLAPPED overlapped = {};
        overlapped.hEvent = event;
        if (!WriteFile(pipe, line.data(), (DWORD)line.size(), nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING)
            return false;

        DWORD written = 0;
        if (WaitForSingleObject(event, kWriteTimeoutMs) != WAIT_OBJECT_0)
        {
            CancelIo(pipe);
            GetOverlappedResult(pipe, &overlapped, &written, TRUE);
            return false;
        }
        return GetOverlappedResult(pipe, &overlapped, &written, FALSE) && written == line.size();
    }

    void ServeConnection(HANDLE pipe, std::shared_ptr<std::atomic<bool>> finished)
    {
        const HANDLE event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        std::string pending, line;
        while (ReadLine(pipe, event, pending, line))
        {
            const std::string response = line == "ping" ?
                "{\"ok\":true,\"frame\":" + std::to_string(s_FrameCount.load()) + "}" :
                AskMainThread(pipe, line);
            if (response.empty() || !WriteLine(pipe, event, response))
                break;
        }
        CloseHandle(event);
        CloseHandle(pipe);
        *finished = true;
    }

    // 끝난 연결의 thread handle이 쌓이지 않게 새 연결을 받을 때 정리한다. s_Mutex를 잡고 부른다.
    void JoinFinishedConnections(void)
    {
        for (auto connection = s_Connections.begin(); connection != s_Connections.end();)
        {
            if (*connection->finished)
            {
                connection->thread.join();
                connection = s_Connections.erase(connection);
            }
            else
                ++connection;
        }
    }

    // 연결마다 thread를 두어, 한 client가 명령의 응답을 기다리는 동안 다른 client가 ping할 수 있게 한다.
    void Listen(void)
    {
        const HANDLE event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        DWORD firstInstanceFlag = FILE_FLAG_FIRST_PIPE_INSTANCE;
        while (WaitForSingleObject(s_StopEvent, 0) != WAIT_OBJECT_0)
        {
            const HANDLE pipe = CreateNamedPipeW(s_PipePath.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | firstInstanceFlag,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES,
                kPipeBufferSize, kPipeBufferSize, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                Utility::Printf(L"Control channel: cannot create %s (error %u)\n", s_PipePath.c_str(), GetLastError());
                break;
            }
            firstInstanceFlag = 0;

            OVERLAPPED overlapped = {};
            overlapped.hEvent = event;
            bool connected = ConnectNamedPipe(pipe, &overlapped) != FALSE;
            const DWORD error = GetLastError();
            DWORD transferred = 0;
            if (!connected && error == ERROR_IO_PENDING)
                connected = WaitForIo(pipe, overlapped, transferred);
            else if (!connected && error == ERROR_PIPE_CONNECTED)
                connected = true;

            if (!connected)
            {
                CloseHandle(pipe);
                continue;
            }

            std::lock_guard<std::mutex> lock(s_Mutex);
            JoinFinishedConnections();
            const auto finished = std::make_shared<std::atomic<bool>>(false);
            s_Connections.push_back({ std::thread(ServeConnection, pipe, finished), finished });
        }
        CloseHandle(event);
    }

    void RunDueActions(void)
    {
        const uint64_t frame = Graphics::GetFrameCount();
        std::vector<std::function<void()>> dueActions;
        for (auto scheduled = s_ScheduledActions.begin(); scheduled != s_ScheduledActions.end();)
        {
            if (scheduled->frame <= frame)
            {
                dueActions.push_back(scheduled->action);
                scheduled = s_ScheduledActions.erase(scheduled);
            }
            else
                ++scheduled;
        }
        for (const auto& action : dueActions)
            action();
    }

    void Execute(const std::shared_ptr<Request>& request)
    {
        const Reply reply(request);
        const size_t nameEnd = request->line.find(' ');
        const std::string name = request->line.substr(0, nameEnd);
        const size_t argumentsStart = request->line.find_first_not_of(' ', nameEnd);
        const std::string arguments = argumentsStart == std::string::npos ? std::string() : request->line.substr(argumentsStart);

        const auto command = s_Commands.find(name);
        if (command == s_Commands.end())
        {
            reply.Error("unknown command '" + name + "'. Send help for the commands.");
            return;
        }

        try
        {
            command->second.handler(arguments, reply);
        }
        catch (const std::exception& exception)
        {
            reply.Error(std::string("'") + name + "' failed: " + exception.what());
        }
    }
}

ControlChannel::Fields& ControlChannel::Fields::Text(const std::string& key, const std::string& value)
{
    m_Members.push_back({ key, Quote(value), nullptr });
    return *this;
}

ControlChannel::Fields& ControlChannel::Fields::Float(const std::string& key, float value)
{
    char text[32] = "null";
    if (std::isfinite(value))
        sprintf_s(text, "%.9g", value);
    m_Members.push_back({ key, text, nullptr });
    return *this;
}

ControlChannel::Fields& ControlChannel::Fields::Integer(const std::string& key, int64_t value)
{
    m_Members.push_back({ key, std::to_string(value), nullptr });
    return *this;
}

ControlChannel::Fields& ControlChannel::Fields::Flag(const std::string& key, bool value)
{
    m_Members.push_back({ key, value ? "true" : "false", nullptr });
    return *this;
}

ControlChannel::Fields& ControlChannel::Fields::Object(const std::string& key, const Fields& value)
{
    m_Members.push_back({ key, std::string(), std::make_shared<Fields>(value) });
    return *this;
}

ControlChannel::Fields& ControlChannel::Fields::Append(const Fields& other)
{
    m_Members.insert(m_Members.end(), other.m_Members.begin(), other.m_Members.end());
    return *this;
}

ControlChannel::Fields& ControlChannel::Fields::Merge(const Fields& other)
{
    for (const Member& member : other.m_Members)
    {
        const auto same = std::find_if(m_Members.begin(), m_Members.end(), [&member](const Member& existing) { return existing.key == member.key; });
        if (same != m_Members.end() && same->object != nullptr && member.object != nullptr)
        {
            // object는 다른 Fields와 공유할 수 있으므로 고치기 전에 사본으로 바꾼다.
            same->object = std::make_shared<Fields>(*same->object);
            same->object->Merge(*member.object);
        }
        else
            m_Members.push_back(member);
    }
    return *this;
}

std::string ControlChannel::Fields::ToMembers(void) const
{
    std::string members;
    for (const Member& member : m_Members)
    {
        if (!members.empty())
            members += ",";
        members += Quote(member.key) + ":" + (member.object != nullptr ? "{" + member.object->ToMembers() + "}" : member.value);
    }
    return members;
}

bool ControlChannel::Fields::Select(const std::string& path, Fields& selected, std::string& error) const
{
    const size_t keyEnd = path.find('.');
    const std::string key = path.substr(0, keyEnd);
    const auto member = std::find_if(m_Members.begin(), m_Members.end(), [&key](const Member& member) { return member.key == key; });
    if (member == m_Members.end())
    {
        std::string keys;
        for (const Member& candidate : m_Members)
            keys += (keys.empty() ? "" : ", ") + candidate.key;
        error = "no '" + key + "'. Keys here: " + keys;
        return false;
    }

    selected = Fields();
    if (keyEnd == std::string::npos)
    {
        selected.m_Members.push_back(*member);
        return true;
    }
    if (member->object == nullptr)
    {
        error = "'" + key + "' is a value, not an object";
        return false;
    }
    Fields inner;
    if (!member->object->Select(path.substr(keyEnd + 1), inner, error))
        return false;
    selected.Object(key, inner);
    return true;
}

void ControlChannel::Reply::Ok(const Fields& fields) const
{
    const std::string members = fields.ToMembers();
    Answer(*m_Request, members.empty() ? "{\"ok\":true}" : "{\"ok\":true," + members + "}");
}

void ControlChannel::Reply::Error(const std::string& message, const Fields& fields) const
{
    Answer(*m_Request, MakeError(message, fields));
}

void ControlChannel::Register(const std::string& name, const std::string& usage, const Handler& handler)
{
    s_Commands[name] = { usage, handler };
}

void ControlChannel::SetStatus(const std::string& key, const std::string& value)
{
    s_Status[key] = value;
}

ControlChannel::Fields ControlChannel::GetStatus(void)
{
    Fields fields;
    for (const auto& status : s_Status)
        fields.Text(status.first, status.second);
    return fields;
}

void ControlChannel::RegisterState(const std::string& group, const std::function<Fields(void)>& describe)
{
    s_StateGroups[group] = describe;
}

bool ControlChannel::GetState(const std::string& path, Fields& state, std::string& error)
{
    Fields whole;
    whole.Integer("frame", Graphics::GetFrameCount());
    for (const auto& group : s_StateGroups)
        whole.Object(group.first, group.second());

    if (path.empty())
    {
        state = whole;
        return true;
    }
    return whole.Select(path, state, error);
}

ControlChannel::Fields ControlChannel::GetState(const std::string& path)
{
    Fields state;
    std::string error;
    return GetState(path, state, error) ? state : Fields();
}

void ControlChannel::AfterFrames(uint32_t frames, const std::function<void()>& action)
{
    s_ScheduledActions.push_back({ Graphics::GetFrameCount() + frames, action });
}

void ControlChannel::Initialize(void)
{
    Register("help", "help", [](const std::string&, const Reply& reply)
    {
        Fields commands;
        for (const auto& command : s_Commands)
            commands.Text(command.first, command.second.usage);
        reply.Ok(Fields().Object("commands", commands));
    });
    // "ping" 한 줄은 pipe thread가 답하므로 여기 handler는 인자를 붙여 보낸 ping만 받는다. help에 보이려고 등록한다.
    Register("ping", "ping: answered at once by the pipe thread with the frame count, even while a command or the main loop is stuck",
        [](const std::string&, const Reply& reply) { reply.Ok(Fields().Integer("frame", Graphics::GetFrameCount())); });
    RegisterEngineCommands();

    std::wstring pipeName;
    if (!CommandLineArgs::GetString(L"control", pipeName))
        return;

    s_PipePath = L"\\\\.\\pipe\\" + pipeName;
    s_StopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    s_ListenThread = std::thread(Listen);
}

void ControlChannel::Shutdown(void)
{
    if (!s_ListenThread.joinable())
        return;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Stopping = true;
    }
    s_Answered.notify_all();
    SetEvent(s_StopEvent);
    s_ListenThread.join();

    std::vector<Connection> connections;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        connections.swap(s_Connections);
    }
    for (Connection& connection : connections)
        connection.thread.join();

    CloseHandle(s_StopEvent);
    s_StopEvent = nullptr;
}

void ControlChannel::Update(void)
{
    s_FrameCount = Graphics::GetFrameCount();
    RunDueActions();

    std::shared_ptr<Request> request;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_Executing != nullptr && !s_Executing->answered && !s_Executing->abandoned)
            return;
        s_Executing.reset();
        while (!s_Queue.empty() && s_Queue.front()->abandoned)
            s_Queue.pop_front();
        if (s_Queue.empty())
            return;
        request = s_Queue.front();
        s_Queue.pop_front();
        s_Executing = request;
    }
    Execute(request);
}
