#include "Core/pch.h"
#include "Core/Automation/UnattendedRun.h"

#include <atomic>
#include <crtdbg.h>
#include <csignal>
#include <werapi.h>

#pragma comment(lib, "wer.lib")

namespace
{
    bool s_Enabled = false;

    // heap이 망가졌거나 stack이 거의 남지 않았을 때도 이유를 남기도록 handler는 할당하지 않고 이 buffer에 쓴다.
    char s_ModulePath[MAX_PATH];
    char s_Address[MAX_PATH + 32];
    char s_ReasonLine[MAX_PATH + 96];
    std::atomic_flag s_ExitClaimed = ATOMIC_FLAG_INIT;

    // 여러 thread가 함께 끝내려 하면 먼저 온 thread만 buffer를 쓰고 이유를 남긴다.
    // 나중 thread는 먼저 온 thread가 process를 끝낼 때까지 기다린다.
    void ClaimExit(void)
    {
        if (s_ExitClaimed.test_and_set())
            Sleep(INFINITE);
    }

    // debugger가 붙어 있으면 끝내기 전에 멈춰서 지금처럼 그 자리를 볼 수 있게 한다.
    void ExitWithReason(const char* reason, UINT exitCode)
    {
        if (IsDebuggerPresent())
            __debugbreak();
        sprintf_s(s_ReasonLine, "exit reason=%s\n", reason);
        Utility::Print(s_ReasonLine);
        TerminateProcess(GetCurrentProcess(), exitCode);
    }

    // crash dump는 아직 남기지 않으므로, 주소를 module 기준으로 남겨 map file이나 pdb로 위치를 찾을 수 있게 한다.
    const char* DescribeAddress(const void* address)
    {
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module) &&
            GetModuleFileNameA(module, s_ModulePath, MAX_PATH) != 0)
        {
            const char* separator = strrchr(s_ModulePath, '\\');
            sprintf_s(s_Address, "%s+0x%llx", separator != nullptr ? separator + 1 : s_ModulePath,
                (unsigned long long)((const char*)address - (const char*)module));
        }
        else
        {
            sprintf_s(s_Address, "%p", address);
        }
        return s_Address;
    }

    LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception)
    {
        ClaimExit();
        static char reason[MAX_PATH + 64];
        const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
        const char* address = DescribeAddress(record.ExceptionAddress);
        if (record.ExceptionCode == EXCEPTION_BREAKPOINT)
            sprintf_s(reason, "breakpoint at=%s", address);
        else
            sprintf_s(reason, "exception code=0x%08X at=%s", record.ExceptionCode, address);
        ExitWithReason(reason, record.ExceptionCode);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void __cdecl OnAbort(int)
    {
        ClaimExit();
        ExitWithReason("abort", 3);
    }

    void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        ClaimExit();
        ExitWithReason("invalid_parameter", 3);
    }

#ifdef _DEBUG
    // Debug CRT의 assert와 error는 Abort, Retry, Ignore dialog를 띄운다.
    int __cdecl OnCrtReport(int reportType, char* message, int*)
    {
        if (reportType == _CRT_WARN || IsDebuggerPresent())
            return FALSE;

        ClaimExit();
        Utility::Print(message);
        ExitWithReason(reportType == _CRT_ASSERT ? "crt_assert" : "crt_error", 3);
        return TRUE;
    }
#endif
}

void UnattendedRun::Initialize(void)
{
    uint32_t unattended = 0;
    if (!CommandLineArgs::GetInteger(L"unattended", unattended) || unattended == 0)
        return;
    s_Enabled = true;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // filter를 거치지 않는 fail fast 같은 종료도 Windows Error Reporting dialog 없이 끝나게 한다.
    WerSetFlags(WER_FAULT_REPORTING_NO_UI);
    SetUnhandledExceptionFilter(OnUnhandledException);

    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, OnAbort);
    _set_invalid_parameter_handler(OnInvalidParameter);
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, OnCrtReport);
#endif
}

void UnattendedRun::ReportNormalExit(void)
{
    if (s_Enabled)
        Utility::Print("exit reason=normal\n");
}
