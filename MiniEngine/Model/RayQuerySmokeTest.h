#pragma once

namespace RayQuerySmokeTest
{
    // NULL acceleration structure에 ray를 쏜다. DXR spec은 이 경우 miss로 정하므로
    // 모든 thread가 miss를 보고해야 한다.
    bool Run(void);
}
