#pragma once

// -unattended 1이면 app이 어떻게 끝났는지를 log에 "exit reason=<이유>" 한 줄로 남긴다.
// harness는 exit code가 아니라 이 줄로 종료를 판정하므로 이유의 이름은 여기에만 정한다.
// - normal: message loop가 끝났다.
// - breakpoint at=<module>+<offset>: ASSERT, HALT, DXGI의 break on error, inline raytracing device가 없을 때의 정지.
// - exception code=<code> at=<module>+<offset>: 그 밖의 SEH exception. 잡지 않은 C++ exception은 0xE06D7363이다.
// - abort, invalid_parameter, crt_assert, crt_error: CRT가 끝내려 한 경우.
// 이 줄이 없으면 kill되었거나, __fastfail처럼 filter를 거치지 않고 끝난 것이다.
namespace UnattendedRun
{
    // crash, ASSERT, CRT error가 dialog를 띄우거나 debugger를 기다리지 않고 이유를 남긴 뒤 바로 끝나게 한다.
    // debugger가 붙어 있으면 지금처럼 debugger에서 멈춘다.
    void Initialize(void);
    // message loop가 끝나 정상으로 종료할 때 부른다.
    void ReportNormalExit(void);
}
