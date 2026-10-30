#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// 측정한 값이 어떤 조건에서 나왔는지를 "key=value; key=value" 한 줄로 쓰고 읽는다. Log와 capture가 이 줄을 남기고, Restore가 이 줄로
// 조건을 되돌린다. 줄 하나가 조건 전체를 담으므로 app을 다시 실행해도 같은 image를 다시 만들거나, 같은 조건에서 다른 것을 잴 수 있다.
// 조건은 그것을 가진 기능이 group으로 등록한다.
namespace MeasurementConditions
{
    // key와 value에는 '='와 "; "를 쓰지 않는다.
    typedef std::map<std::string, std::string> Values;

    // line의 끝에 key=value를 "; "로 이어 쓴다.
    void AppendValue(std::string& line, const std::string& key, const std::string& value);
    // key 순서로 한 줄로 쓴다.
    std::string Format(const Values& values);

    // 줄의 key=value들. Log가 앞에 붙인 "[이름] " 같은 말은 첫 key 앞의 "] "까지 건너뛴다.
    Values Parse(const std::string& line);

    // 줄의 value 하나를 읽는다. 앞뒤에 다른 글자가 있으면 false다. ReadFloat는 inf와 nan도 읽는다.
    bool ReadFloat(const std::string& text, float& value);
    bool ReadUInt(const std::string& text, uint32_t& value);

    // group의 read가 줄에서 읽은 것
    struct Reading
    {
        // 없거나 받을 수 없는 key
        std::vector<std::string> unread;
        // 되돌리지 않고 지금과 견주기만 하는 조건이 지금과 다른 점, 되돌려도 같아지지 않는 것
        std::vector<std::string> notes;
        // 모든 group이 다 읽은 뒤에 차례로 부르는, 읽은 값으로 되돌리는 일
        std::vector<std::function<void()>> applies;
    };

    // values의 key를 parse로 읽는다. 없거나 parse가 false면 key를 reading.unread에 더하고 false를 돌려준다.
    bool ReadValue(const Values& values, const std::string& key, Reading& reading, const std::function<bool(const std::string&)>& parse);

    struct Group
    {
        // 지금의 조건을 values에 더한다.
        std::function<void(Values&)> describe;
        // values에서 이 group의 key를 모두 읽어 reading에 더한다. 상태는 applies만 바꾼다.
        std::function<void(const Values&, Reading&)> read;
        // 이 group의 조건이 있는 state의 path. restore 명령이 바꾼 것을 응답할 때 쓴다.
        std::vector<std::string> statePaths;
    };
    void Register(const std::string& name, const Group& group);
    void Unregister(const std::string& name);

    // 등록한 모든 group의 지금 조건
    Values Describe(void);

    // Restore의 결과. unread가 비어 있지 않으면 아무것도 바꾸지 않았다.
    struct Result
    {
        std::vector<std::string> unread;
        std::vector<std::string> notes;
    };
    // 등록한 group의 key 중 하나라도 없거나 받을 수 없으면 아무것도 바꾸지 않는다. 일부만 되돌리면 조건이 다른 image를 같은 조건의
    // image로 잘못 보게 되기 때문이다. 줄에 있는 다른 key (Log의 수치 등)는 읽지 않는다.
    Result Restore(const Values& values);

    // 등록한 group의 statePaths를 모두 모은 것
    std::vector<std::string> GetStatePaths(void);

    // Restore 칸과 button. 붙여 넣은 줄로 Restore하고, 되돌리지 못한 key와 지금과 다른 점을 경고로 보인다.
    class RestoreField
    {
    public:
        void SetLine(const std::string& line);
        const char* GetLine(void) const { return m_Line; }
        // 칸의 줄로 Restore한 뒤 칸을 고치지 않았으면 true
        bool HasResult(void) const { return m_HasResult; }
        void Show(void);

    private:
        char m_Line[4096] = {};
        bool m_HasResult = false;
        Result m_Result;
    };
}
