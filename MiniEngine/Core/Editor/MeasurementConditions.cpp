#include "Core/pch.h"
#include "Core/Editor/MeasurementConditions.h"

void MeasurementConditions::AppendValue(std::string& line, const std::string& key, const std::string& value)
{
    if (!line.empty())
        line += "; ";
    line += key + "=" + value;
}

MeasurementConditions::Values MeasurementConditions::Parse(const std::string& line)
{
    Values values;
    const size_t prefixEnd = line.rfind("] ", line.find('='));
    size_t start = prefixEnd == std::string::npos ? 0 : prefixEnd + 2;
    while (start < line.size())
    {
        const size_t end = std::min(line.find("; ", start), line.size());
        const std::string token = line.substr(start, end - start);
        const size_t separator = token.find('=');
        if (separator != std::string::npos)
            values[token.substr(0, separator)] = token.substr(separator + 1);
        start = end + 2;
    }
    return values;
}
