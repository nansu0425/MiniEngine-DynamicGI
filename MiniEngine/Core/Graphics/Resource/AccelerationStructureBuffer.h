#pragma once

#include "Core/pch.h"
#include "Core/Graphics/Resource/GpuResource.h"

// acceleration structure는 만들 때부터 RAYTRACING_ACCELERATION_STRUCTURE state이고 다른 state로 transition할 수 없다.
// build와 그 결과를 읽는 작업 사이는 UAV barrier로 동기화한다.
class AccelerationStructureBuffer : public GpuResource
{
public:
    void Create(const std::wstring& name, uint64_t sizeInBytes);
};
