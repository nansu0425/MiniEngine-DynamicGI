#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// harness가 -control <pipe>로 연 \\.\pipe\<pipe>에 보낸 명령을 main thread가 frame 사이에서 실행하고 JSON 한 줄로 응답한다.
// 요청은 "<명령> <나머지 줄>" 한 줄이다. Log 줄처럼 '='와 "; "가 든 인자를 escape 없이 넘기려고 나머지 줄은 그대로 handler에 준다.
// 응답은 {"ok":true,...} 또는 {"ok":false,"error":"...",...}이다. 명령은 하나씩 실행하고, 앞 명령이 응답하기 전에는 다음 명령을 꺼내지 않는다.
// 응답을 기다리던 client가 연결을 끊으면 그 명령은 버리고 다음 명령을 꺼낸다.
// ping만은 main thread를 거치지 않고 바로 지금까지 그린 frame 수로 응답하므로, harness는 이것으로 main loop가 멈췄는지 본다.
namespace ControlChannel
{
    // 응답 JSON object의 member. 넣은 순서대로 쓴다.
    class Fields
    {
    public:
        Fields& Text(const std::string& key, const std::string& value);
        // float로 다시 읽어도 같은 값이 되는 자릿수로 쓴다.
        Fields& Float(const std::string& key, float value);
        Fields& Integer(const std::string& key, int64_t value);
        Fields& Flag(const std::string& key, bool value);
        Fields& Object(const std::string& key, const Fields& value);

        // 중괄호 없이 member만 쓴다.
        std::string ToMembers(void) const;

    private:
        std::vector<std::pair<std::string, std::string>> m_Members;
    };

    struct Request;

    // 명령 하나에 한 번 응답한다. 복사해 두었다가 뒤 frame에 응답해도 된다.
    class Reply
    {
    public:
        explicit Reply(const std::shared_ptr<Request>& request) : m_Request(request) {}

        void Ok(const Fields& fields = Fields()) const;
        void Error(const std::string& message, const Fields& fields = Fields()) const;

    private:
        std::shared_ptr<Request> m_Request;
    };

    // arguments는 명령 이름 뒤의 나머지 줄이다.
    typedef std::function<void(const std::string& arguments, const Reply& reply)> Handler;

    // help가 usage를 그대로 돌려주므로 명령의 사용법은 여기에만 쓴다.
    void Register(const std::string& name, const std::string& usage, const Handler& handler);

    // status에 더할 값. app이 정하는 것처럼 engine이 모르는 상태를 status 하나로 보게 한다.
    void SetStatus(const std::string& key, const std::string& value);
    Fields GetStatus(void);

    // 명령의 효과가 화면에 나타난 뒤에 응답하도록, frames만큼 frame을 더 그린 뒤 main thread에서 action을 부른다.
    void AfterFrames(uint32_t frames, const std::function<void()>& action);

    // -control <pipe>가 없으면 pipe를 열지 않는다. 명령은 app의 초기화가 끝난 상태를 보도록 초기화의 마지막에 연다.
    void Initialize(void);
    void Shutdown(void);
    // frame마다 main thread가 부른다. 받은 명령과 때가 된 AfterFrames의 action을 실행한다.
    void Update(void);
}
