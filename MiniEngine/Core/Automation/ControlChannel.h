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
        // other의 member를 뒤에 붙인다. 여러 group의 state를 한 응답에 담을 때 쓰고, key가 겹치지 않게 부른다.
        Fields& Append(const Fields& other);

        // 중괄호 없이 member만 쓴다.
        std::string ToMembers(void) const;

        // path가 가리키는 member 하나만 남긴 사본을 selected에 쓴다. path는 바깥 key부터 '.'로 잇는다.
        // 없는 key를 만나면 false를 돌려주고, error에 그 자리에 있는 key를 쓴다.
        bool Select(const std::string& path, Fields& selected, std::string& error) const;

    private:
        struct Member
        {
            std::string key;
            // object가 아닐 때 JSON으로 쓴 값
            std::string value;
            std::shared_ptr<Fields> object;
        };
        std::vector<Member> m_Members;
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

    // state 명령이 돌려주는 scene, editor, engine 상태의 한 group. camera처럼 engine이 모르는 상태는 app이 등록한다.
    // state는 harness가 판정하는 SSOT이므로 상태를 바꾼 명령은 바뀐 부분을 GetState로 응답한다.
    // status와 pose처럼 같은 값을 예전 형식으로 따로 돌려주는 명령도 있고, 그 값이 state와 같은지는 selftest가 확인한다.
    void RegisterState(const std::string& group, const std::function<Fields(void)>& describe);
    // frame과 등록한 group 전부. path가 있으면 그 member만 남긴다 (Fields::Select). 없는 path는 false를 돌려준다.
    bool GetState(const std::string& path, Fields& state, std::string& error);
    // 상태를 바꾼 명령이 바뀐 부분을 state와 같은 형식으로 응답할 때 쓴다. 없는 path면 빈 Fields다.
    Fields GetState(const std::string& path = std::string());

    // 명령의 효과가 화면에 나타난 뒤에 응답하도록, frames만큼 frame을 더 그린 뒤 main thread에서 action을 부른다.
    void AfterFrames(uint32_t frames, const std::function<void()>& action);

    // -control <pipe>가 없으면 pipe를 열지 않는다. 명령은 app의 초기화가 끝난 상태를 보도록 초기화의 마지막에 연다.
    void Initialize(void);
    void Shutdown(void);
    // frame마다 main thread가 부른다. 받은 명령과 때가 된 AfterFrames의 action을 실행한다.
    void Update(void);
}
