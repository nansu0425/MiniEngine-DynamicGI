#pragma once

#include <string>

// D3D12 debug layer의 message를 -log file에 쓴다. harness는 이 줄로 message를 세므로 줄의 형식은 여기에만 정한다.
// - 시작할 때 한 번 "D3D12 messages: logged"를 쓴다. GPU-based validation이 켜졌으면 ", GPU-based validation on"을 붙인다.
//   쓸 수 없으면 대신 "D3D12 messages: not logged (<이유>)"를 쓴다.
// - message마다 "D3D12 <CORRUPTION|ERROR|WARNING|INFO|MESSAGE> #<id> <CATEGORY> frame=<n>: <description>"을 쓴다.
//   description의 CR, LF, TAB은 공백으로 바꾼다. Start 전에 device의 info queue에 쌓인 message는 상태 줄 바로 뒤에 쓴다.
//   device를 고르는 동안 버린 다른 adapter의 device와 Stop 뒤의 message는 남지 않는다.
// - 같은 ID가 kLoggedPerId번을 넘으면 "D3D12 #<id>: logged <kLoggedPerId> times, the rest are only counted"를 한 번 쓰고 그 뒤로는 세기만 한다.
// - "D3D12 live objects at exit:" 줄 뒤로는 제한 없이 쓴다.
// - 끝날 때 제한에 걸린 ID마다 그 줄 앞까지 나온 수를 "D3D12 #<id> total=<n>"으로 쓴다.
namespace D3D12MessageLog
{
    // 매 frame 나오는 message가 log를 덮지 않게 ID마다 이만큼만 쓴다.
    constexpr uint64_t kLoggedPerId = 10;

    struct Status
    {
        bool logged = false;
        std::string notLoggedReason;
        bool gpuBasedValidation = false;
        uint64_t countBySeverity[D3D12_MESSAGE_SEVERITY_MESSAGE + 1] = {};
    };

    void Start(ID3D12Device* device, bool gpuBasedValidation);
    void BeginLiveObjectReport(void);
    // device를 놓기 전에 부른다.
    void Stop(ID3D12Device* device);
    Status GetStatus(void);
    const char* GetSeverityName(D3D12_MESSAGE_SEVERITY severity);
}
