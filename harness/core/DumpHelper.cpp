#include "core/DumpHelper.h"

#include <windows.h>
#include <dbghelp.h>
#include <malloc.h>
#include <process.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <exception>
#include <new>
#include <string>

namespace
{
    std::wstring g_dumpDir;
    std::wstring g_dumpPath;

    HANDLE g_workerStart = nullptr;
    HANDLE g_workerDone = nullptr;
    EXCEPTION_RECORD g_faultRecord{};
    CONTEXT g_faultContext{};
    DWORD g_faultThreadId = 0;
    bool g_haveFault = false;
    volatile LONG g_capturing = 0;
    volatile LONG g_exitCode = 0;

    const MINIDUMP_TYPE kCrashDumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo | MiniDumpWithProcessThreadData);

    const MINIDUMP_TYPE kHangDumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithHandleData);

    // Heap corruption is raised while the heap lock is held, so a full-memory dump
    // (which walks every heap) deadlocks. A normal dump plus thread info keeps the
    // stacks and the exception record, which is what this fault needs.
    const MINIDUMP_TYPE kHeapFaultDumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules);

    MINIDUMP_TYPE g_activeCrashType = kCrashDumpType;

    std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
    {
        if (dir.empty())
        {
            return name;
        }
        wchar_t last = dir[dir.size() - 1];
        if (last == L'\\' || last == L'/')
        {
            return dir + name;
        }
        return dir + L"\\" + name;
    }

    std::wstring ExeDirectory()
    {
        wchar_t buffer[MAX_PATH * 4] = {};
        DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(_countof(buffer)));
        if (length == 0 || length >= _countof(buffer))
        {
            return L".";
        }
        std::wstring path(buffer, length);
        std::wstring::size_type slash = path.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? std::wstring(L".") : path.substr(0, slash);
    }

    // <exe>\..\..\dumps : CrashHarness.exe lives in output\bin, dumps in output\dumps.
    std::wstring ResolveDumpDirectory()
    {
        std::wstring exeDir = ExeDirectory();
        std::wstring outputDir = JoinPath(exeDir, L"..");
        std::wstring dumps = JoinPath(outputDir, L"dumps");
        wchar_t full[MAX_PATH * 4] = {};
        if (GetFullPathNameW(dumps.c_str(), static_cast<DWORD>(_countof(full)), full, nullptr) != 0)
        {
            dumps = full;
        }
        CreateDirectoryW(dumps.c_str(), nullptr);
        return dumps;
    }

    bool WriteDump(const std::wstring& path, DWORD threadId, EXCEPTION_POINTERS* pointers, MINIDUMP_TYPE type)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            std::fprintf(stderr, "[dump] cannot create %ls (error %lu)\n", path.c_str(), GetLastError());
            return false;
        }

        MINIDUMP_EXCEPTION_INFORMATION info{};
        MINIDUMP_EXCEPTION_INFORMATION* infoPtr = nullptr;
        if (pointers != nullptr)
        {
            info.ThreadId = threadId;
            info.ExceptionPointers = pointers;
            info.ClientPointers = FALSE;
            infoPtr = &info;
        }

        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                   infoPtr, nullptr, nullptr);
        if (!ok)
        {
            std::fprintf(stderr, "[dump] MiniDumpWriteDump failed (error %lu)\n", GetLastError());
        }
        CloseHandle(file);
        return ok == TRUE;
    }

    // Runs on its own thread with a healthy stack, which matters for stack overflow.
    // It also owns process termination, so the faulting thread never has to run
    // anything that needs stack after the dump is requested.
    DWORD WINAPI DumpWorker(LPVOID)
    {
        WaitForSingleObject(g_workerStart, INFINITE);
        EXCEPTION_POINTERS pointers{ &g_faultRecord, &g_faultContext };
        if (!WriteDump(g_dumpPath, g_faultThreadId, g_haveFault ? &pointers : nullptr, g_activeCrashType))
        {
            // A dump walk can fail when the fault also damaged the heap; retry with
            // the lightest shape so the analysis still gets stacks + the exception.
            WriteDump(g_dumpPath, g_faultThreadId, g_haveFault ? &pointers : nullptr, kHangDumpType);
        }
        SetEvent(g_workerDone);
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(g_exitCode));
        return 0;
    }

    [[noreturn]] void CaptureAndExit(DWORD exitCode, EXCEPTION_POINTERS* pointers)
    {
        // A second fault while the first one is dumping must not truncate the file:
        // park this thread and let the first capturer terminate the process.
        if (InterlockedExchange(&g_capturing, 1) != 0)
        {
            for (;;)
            {
                Sleep(1000);
            }
        }

        InterlockedExchange(&g_exitCode, static_cast<LONG>(exitCode));
        if (g_workerStart == nullptr)
        {
            TerminateProcess(GetCurrentProcess(), exitCode);
        }

        if (pointers != nullptr && pointers->ExceptionRecord != nullptr && pointers->ContextRecord != nullptr)
        {
            std::memcpy(&g_faultRecord, pointers->ExceptionRecord, sizeof(g_faultRecord));
            std::memcpy(&g_faultContext, pointers->ContextRecord, sizeof(g_faultContext));
            g_faultThreadId = GetCurrentThreadId();
            g_haveFault = true;
        }

        SetEvent(g_workerStart);

        // Do not call WaitForSingleObject here: on a stack overflow this frame has
        // one guard page at most, and waiting again can overflow a second time.
        for (;;)
        {
            Sleep(1000);
        }
    }

    bool IsFatalCode(DWORD code)
    {
        switch (code)
        {
        case 0xC0000005: // access violation
        case 0xC0000374: // heap corruption
        case 0xC00000FD: // stack overflow
        case 0xC0000409: // fast fail / stack cookie
        case 0xC000001D: // illegal instruction
        case 0xC0000094: // integer divide by zero
        case 0xC0000096: // privileged instruction
        case 0xC000008C: // array bounds exceeded
        case 0xC0000135: // dll not found
            return true;
        default:
            return false;
        }
    }

    LONG CALLBACK VectoredHandler(EXCEPTION_POINTERS* pointers)
    {
        DWORD code = pointers->ExceptionRecord->ExceptionCode;
        if (!IsFatalCode(code))
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        if (code == 0xC00000FD)
        {
            // Restore the guard page so this handler still has stack to call into,
            // then go straight to capture: no fprintf on an exhausted stack.
            _resetstkoflw();
            CaptureAndExit(code, pointers);
        }

        if (code == 0xC0000374)
        {
            g_activeCrashType = kHeapFaultDumpType;
        }

        std::fprintf(stderr, "[dump] fatal exception 0x%08lX on thread %lu\n", code, GetCurrentThreadId());
        CaptureAndExit(code, pointers);
    }

    LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* pointers)
    {
        DWORD code = pointers->ExceptionRecord->ExceptionCode;
        std::fprintf(stderr, "[dump] unhandled exception 0x%08lX\n", code);
        CaptureAndExit(code, pointers);
    }

    void OnPureCall()
    {
        std::fprintf(stderr, "[dump] _purecall\n");
        CaptureAndExit(0xC0000409, nullptr);
    }

    void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        std::fprintf(stderr, "[dump] invalid parameter\n");
        CaptureAndExit(0xC0000409, nullptr);
    }

    void OnTerminate()
    {
        std::fprintf(stderr, "[dump] std::terminate\n");
        CaptureAndExit(0xC0000409, nullptr);
    }

    void OnAbort(int)
    {
        std::fprintf(stderr, "[dump] abort()\n");
        CaptureAndExit(0xC0000409, nullptr);
    }

    void InstallHandlers()
    {
        // No WER / GPF dialog: this process must never block a scripted run.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_invalid_parameter_handler(&OnInvalidParameter);
        _set_purecall_handler(&OnPureCall);
        std::set_terminate(&OnTerminate);
        std::signal(SIGABRT, &OnAbort);

        AddVectoredExceptionHandler(1, &VectoredHandler);
        SetUnhandledExceptionFilter(&UnhandledFilter);
    }
}

void DumpHelper::Init(const std::string& caseName)
{
    g_dumpDir = ResolveDumpDirectory();
    std::wstring wideName(caseName.begin(), caseName.end());
    if (wideName.size() < 4 || wideName.compare(wideName.size() - 4, 4, L".dmp") != 0)
    {
        wideName += L".dmp";
    }
    g_dumpPath = JoinPath(g_dumpDir, wideName);

    g_workerStart = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_workerDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_workerStart == nullptr || g_workerDone == nullptr)
    {
        std::fprintf(stderr, "[dump] CreateEvent failed (%lu)\n", GetLastError());
        return;
    }

    HANDLE worker = CreateThread(nullptr, 1024 * 1024, &DumpWorker, nullptr, 0, nullptr);
    if (worker != nullptr)
    {
        CloseHandle(worker);
    }

    InstallHandlers();
    std::fprintf(stderr, "[dump] target: %s\n", DumpPath().c_str());
}

void DumpHelper::StartWatchdog(const std::string& dumpName, int delayMs, bool fullMemory)
{
    struct WatchdogContext
    {
        std::wstring path;
        int delayMs;
        bool fullMemory;
    };

    std::wstring name = dumpName.empty()
        ? g_dumpPath
        : JoinPath(g_dumpDir, std::wstring(dumpName.begin(), dumpName.end()) + L".dmp");

    auto* context = new WatchdogContext{ name, delayMs, fullMemory };
    HANDLE thread = CreateThread(nullptr, 0, [](LPVOID raw) -> DWORD {
        auto* ctx = static_cast<WatchdogContext*>(raw);
        Sleep(static_cast<DWORD>(ctx->delayMs));
        MINIDUMP_TYPE type = ctx->fullMemory
            ? static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData |
                                         MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                                         MiniDumpWithFullMemoryInfo | MiniDumpWithProcessThreadData)
            : kHangDumpType;
        WriteDump(ctx->path, 0, nullptr, type);
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;
    }, context, 0, nullptr);

    if (thread == nullptr)
    {
        std::fprintf(stderr, "[dump] watchdog thread creation failed (%lu)\n", GetLastError());
        return;
    }
    CloseHandle(thread);
    std::fprintf(stderr, "[dump] watchdog armed: %d ms, full=%d\n", delayMs, fullMemory ? 1 : 0);
}

std::string DumpHelper::DumpPath()
{
    if (g_dumpPath.empty())
    {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, g_dumpPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 1)
    {
        WideCharToMultiByte(CP_UTF8, 0, g_dumpPath.c_str(), -1, &narrow[0], size, nullptr, nullptr);
        narrow.resize(static_cast<size_t>(size - 1));
    }
    return narrow;
}
