#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GpuResource;

namespace GIProfiling
{
    enum class ResourceRole
    {
        kProbeData,
        kRayBuffer,
        kScreen,
        kDebug,
        kShared,
    };

    struct Resource
    {
        const GpuResource* resource;
        ResourceRole role;
    };

    struct PassUnits
    {
        const wchar_t* scope;
        const char* unit;
        uint64_t count;
    };

    struct Condition
    {
        std::string name;
        std::string value;
    };

    struct Workload
    {
        std::vector<PassUnits> passes;
        std::vector<Condition> conditions;
    };

    struct Scopes
    {
        const wchar_t* pipeline;
        std::vector<const wchar_t*> debug;
    };

    struct Report
    {
        Scopes scopes;
        Workload workload;
        std::vector<Resource> resources;
    };
}
