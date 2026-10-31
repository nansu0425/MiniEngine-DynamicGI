#include "Core/pch.h"
#include "Core/Automation/SettingsAssignment.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace ControlChannel;

namespace
{
    // 유한한 숫자는 float로 비교하되, ExpVar처럼 log2로 저장했다가 되돌리며 생기는 몇 ulp의 차이는 같은 값으로 본다. inf와 nan은
    // 글자로 비교한다. /fp:fast에서는 nan과의 비교가 참이 될 수 있어, 받지 않은 nan을 받은 것으로 볼 수 있다.
    bool IsAppliedAsRequested(const std::string& requested, const std::string& applied)
    {
        char* requestedEnd = nullptr;
        char* appliedEnd = nullptr;
        const float requestedNumber = strtof(requested.c_str(), &requestedEnd);
        const float appliedNumber = strtof(applied.c_str(), &appliedEnd);
        const bool bothNumbers = requestedEnd != requested.c_str() && *requestedEnd == '\0' && appliedEnd != applied.c_str() && *appliedEnd == '\0';
        if (!bothNumbers || !std::isfinite(requestedNumber) || !std::isfinite(appliedNumber))
            return requested == applied;
        return std::fabs(requestedNumber - appliedNumber) <= 1e-6f * std::max(std::fabs(requestedNumber), std::fabs(appliedNumber));
    }
}

bool SettingsAssignment::Split(const std::string& arguments, std::string& name, std::string& assignments)
{
    const size_t nameEnd = arguments.find(':');
    if (nameEnd == std::string::npos)
        return false;
    name = Utility::Trim(arguments.substr(0, nameEnd));
    assignments = arguments.substr(nameEnd + 1);
    return true;
}

bool SettingsAssignment::Assign(const std::string& name, const std::string& assignments, const std::function<void(Values&)>& read,
    const std::function<void(const Values&)>& apply, Fields& applied, const Reply& reply)
{
    Values values;
    read(values);
    const Values original = values;

    std::vector<std::pair<std::string, std::string>> requests;
    for (size_t start = 0; start < assignments.size();)
    {
        const size_t end = std::min(assignments.find("; ", start), assignments.size());
        const std::string assignment = assignments.substr(start, end - start);
        start = end + 2;
        if (Utility::Trim(assignment).empty())
            continue;

        const size_t separator = assignment.find('=');
        const std::string key = Utility::Trim(assignment.substr(0, separator));
        if (separator == std::string::npos || values.find(key) == values.end())
        {
            reply.Error("unknown key '" + key + "' in " + name + ". Nothing was set.");
            return false;
        }
        const std::string value = Utility::Trim(assignment.substr(separator + 1));
        values[key] = value;
        requests.emplace_back(key, value);
    }

    apply(values);
    read(values);
    Fields rejected;
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
        apply(original);
        reply.Error("the section did not take some values, so nothing was set. rejected has what they were read back as.",
            Fields().Object("rejected", rejected));
        return false;
    }
    return true;
}

Fields SettingsAssignment::Describe(const Values& values)
{
    Fields fields;
    for (const auto& value : values)
        fields.Text(value.first, value.second);
    return fields;
}
