#include <BranchInfo.h>
#include "CrashHandler.h"
#include <dbghelp.h>
#include <FunctionHook.hpp>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <strsafe.h>
#include <tlhelp32.h>
#include <vector>

using time_point = std::chrono::system_clock::time_point;

std::string SerializeTimePoint(const time_point& time, const std::string& format)
{
    std::time_t tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm = *std::gmtime(&tt); // GMT (UTC)
    // std::tm tm = *std::localtime(&tt); //Locale time-zone, usually UTC by default.
    std::stringstream ss;
    ss << std::put_time(&tm, format.c_str());
    return ss.str();
}

// Unwinds with each module's own unwind tables (ntdll only, no loader lock), so it is
// safe on a suspended thread. Returns the number of return addresses written.
static int CaptureBacktrace(const CONTEXT& acContext, DWORD64* apFrames, int aMaxFrames)
{
    CONTEXT context = acContext;
    int count = 0;
    // Rip 0 on frame 0 is a call through a null pointer: unwind it like a leaf below.
    while (count < aMaxFrames && (context.Rip || count == 0))
    {
        apFrames[count++] = context.Rip;

        DWORD64 imageBase = 0;
        auto* pFunction = context.Rip ? RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr) : nullptr;
        if (pFunction)
        {
            void* pHandlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, pFunction, &context, &pHandlerData,
                             &establisherFrame, nullptr);
        }
        else if (count == 1)
        {
            // Leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += sizeof(DWORD64);
        }
        else
            break; // No unwind data mid-stack: JIT code or garbage, stop rather than guess.
    }
    return count;
}

// Logs module+offset per frame.
// Symbolize afterwards with scripts/re/symbolize_crash.py (engine IDs + our source lines).
static void LogFrames(const DWORD64* apFrames, int aCount)
{
    for (int frame = 0; frame < aCount; ++frame)
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(apFrames[frame]), &module);
        char path[MAX_PATH] = "?";
        if (module)
            GetModuleFileNameA(module, path, MAX_PATH);
        spdlog::error("  backtrace #{:02} {}+{:#x}", frame, std::filesystem::path(path).filename().string(),
                      apFrames[frame] - reinterpret_cast<uintptr_t>(module));
    }
}

static void LogBacktrace(const CONTEXT& acContext)
{
    DWORD64 frames[48];
    LogFrames(frames, CaptureBacktrace(acContext, frames, 48));
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    static int alreadyCrashed = 0;
    auto retval = EXCEPTION_CONTINUE_SEARCH;

    // Serialize 
    static std::mutex singleThreaded;
    const std::lock_guard lock{singleThreaded};

    // Check for severe, not continuable and not software-originated exception
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        alreadyCrashed++ == 0)
    {
        spdlog::critical ("{}: : crash occurred!", __FUNCTION__); 
        
        spdlog::error("{}: : exception code is {:x}, at address {}, flags {:x} ", __FUNCTION__,
                      pExceptionInfo->ExceptionRecord->ExceptionCode,
                      pExceptionInfo->ExceptionRecord->ExceptionAddress,
                      pExceptionInfo->ExceptionRecord->ExceptionFlags);
        LogBacktrace(*pExceptionInfo->ContextRecord);
        spdlog::default_logger()->flush();

#if (IS_MASTER)
        volatile static bool bMiniDump = false;
#else
        volatile static bool bMiniDump = true;
#endif
        if (bMiniDump)
        {
            HANDLE hDumpFile = NULL;
            MINIDUMP_EXCEPTION_INFORMATION M;
            char dumpPath[MAX_PATH];

            M.ThreadId = GetCurrentThreadId();
            M.ExceptionPointers = pExceptionInfo;
            M.ClientPointers = 0;

            std::ostringstream oss;
            oss << "crash_" << SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S")
                << ".dmp";

            GetModuleFileNameA(NULL, dumpPath, sizeof(dumpPath));
            std::filesystem::path modulePath(dumpPath);
            auto subPath = modulePath.parent_path();

            CrashHandler::RemovePreviousDump(subPath);

            subPath /= oss.str();

            hDumpFile = CreateFileA(subPath.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, NULL);

            // baseline settings from https://stackoverflow.com/a/63123214/5273909
            auto dumpSettings = MiniDumpWithDataSegs | MiniDumpWithProcessThreadData | MiniDumpWithHandleData |
                                MiniDumpWithThreadInfo |
                                /*
                                //MiniDumpWithPrivateReadWriteMemory | // this one gens bad dump
                                MiniDumpWithUnloadedModules |
                                MiniDumpWithFullMemoryInfo |
                                MiniDumpWithTokenInformation |
                                MiniDumpWithPrivateWriteCopyMemory |
                                */
                                0;

            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, (MINIDUMP_TYPE)dumpSettings,
                              (pExceptionInfo) ? &M : NULL, NULL, NULL);

            if (!hDumpFile)
                spdlog::critical("{}: : coredump may have failed.", __FUNCTION__);
            else
            {
                CloseHandle(hDumpFile);
                spdlog::critical("{}: : coredump created -> flush logs.", __FUNCTION__);
            }
        }

        spdlog::default_logger()->flush();

        // Something in STR breaks top-level unhandled exception filters.
        // The Win API for them is pretty clunky (non-atomic, not chainable), 
        // but they can do some important things. If someone actually set one
        // they probably meant it; make sure it actually runs.
        // This will make more CrashLogger mods work with STR.

        // Get the current unhandled exception filter. If it has changed
        // from when STR started up, invoke it here.
        LPTOP_LEVEL_EXCEPTION_FILTER pCurrentUnhandledExceptionFilter = SetUnhandledExceptionFilter(CrashHandler::GetOriginalUnhandledExceptionFilter());
        SetUnhandledExceptionFilter(pCurrentUnhandledExceptionFilter);
        if (pCurrentUnhandledExceptionFilter != CrashHandler::GetOriginalUnhandledExceptionFilter())
        {
            spdlog::critical("{}: : UnhandledExceptionFilter() workaround triggered.", __FUNCTION__);

            singleThreaded.unlock();        // Might reenter, but is safe at this point.
            if ((*pCurrentUnhandledExceptionFilter)(pExceptionInfo) == EXCEPTION_CONTINUE_EXECUTION)
                retval = EXCEPTION_CONTINUE_EXECUTION;
            singleThreaded.lock();
        }
    }
    return retval;
}

// Logs who ends the process: a clean exit (code 0) otherwise leaves no trace in our log.
using TExitProcess = void(WINAPI*)(UINT);
static TExitProcess RealExitProcess = nullptr;

static void WINAPI HookExitProcess(UINT aExitCode)
{
    spdlog::error("ExitProcess({}) called", aExitCode);
    CONTEXT context{};
    RtlCaptureContext(&context);
    LogBacktrace(context);
    spdlog::default_logger()->flush();
    RealExitProcess(aExitCode);
}

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::m_pUnhandled;
CrashHandler::CrashHandler()
{
    // Record the original (or as close as we can get) top-level unhandled exception handler.
    // We grab this so we can see if it is changed, presumably by a mod or even graphics drivers.
    // Something in STR breaks unhandled exception handling, so we'll fake it if necessary.
    // This is the only way to get the current setting, but the race is small.
    m_pUnhandled = SetUnhandledExceptionFilter(NULL);
    SetUnhandledExceptionFilter(m_pUnhandled);

    m_handler = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);

    RealExitProcess = reinterpret_cast<TExitProcess>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess"));
    if (RealExitProcess)
        TP_HOOK_IMMEDIATE(&RealExitProcess, &HookExitProcess);
}

static std::atomic<bool> s_watchdogStop{false};
static HANDLE s_watchdogThread = nullptr;

// The plugin unloads (hot reload): nothing of ours may run afterwards.
CrashHandler::~CrashHandler()
{
    if (m_handler)
        RemoveVectoredExceptionHandler(m_handler);
    if (s_watchdogThread)
    {
        s_watchdogStop = true;
        WaitForSingleObject(s_watchdogThread, 3000);
        CloseHandle(s_watchdogThread);
        s_watchdogThread = nullptr;
    }
}

void CrashHandler::RemovePreviousDump(std::filesystem::path path)
{
    for (auto& entry : std::filesystem::directory_iterator(path))
    {
        if (entry.path().string().find("crash") != std::string::npos)
        {
            DeleteFileA(entry.path().string().c_str());
        }
    }
}

static std::atomic<uint64_t> s_mainLoopTicks{0};
static std::atomic<void (*)()> s_hangDiagnostic{nullptr};

void CrashHandler::SetHangDiagnostic(void (*apDiagnostic)()) noexcept
{
    s_hangDiagnostic = apDiagnostic;
}

// After a stall: the stack of every other thread (a stuck worker the main thread waits for is
// otherwise invisible). Each thread is suspended only while capturing.
static void LogOtherThreadsInOurCode(DWORD aMainThreadId)
{
    HMODULE ourModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&LogOtherThreadsInOurCode), &ourModule);
    const auto cOurBase = reinterpret_cast<DWORD64>(ourModule);
    DWORD64 ourEnd = cOurBase;
    {
        const auto* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(ourModule);
        const auto* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(ourModule) + pDos->e_lfanew);
        ourEnd = cOurBase + pNt->OptionalHeader.SizeOfImage;
    }

    struct Captured
    {
        DWORD Id;
        DWORD64 Frames[32];
        int Count;
    };
    std::vector<DWORD> threadIds;
    const HANDLE cSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (cSnapshot == INVALID_HANDLE_VALUE)
        return;
    THREADENTRY32 entry{sizeof(entry)};
    for (BOOL more = Thread32First(cSnapshot, &entry); more; more = Thread32Next(cSnapshot, &entry))
        if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != aMainThreadId &&
            entry.th32ThreadID != GetCurrentThreadId())
            threadIds.push_back(entry.th32ThreadID);
    CloseHandle(cSnapshot);

    std::vector<Captured> captured(threadIds.size());
    size_t used = 0;
    for (const DWORD cId : threadIds)
    {
        const HANDLE cThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, cId);
        if (!cThread)
            continue;
        if (SuspendThread(cThread) != static_cast<DWORD>(-1))
        {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            Captured& slot = captured[used];
            slot.Id = cId;
            slot.Count = GetThreadContext(cThread, &context) ? CaptureBacktrace(context, slot.Frames, 32) : 0;
            ResumeThread(cThread);
            ++used;
        }
        CloseHandle(cThread);
    }

    for (size_t i = 0; i < used; ++i)
    {
        const Captured& cThread = captured[i];
        const bool cInOurCode = std::any_of(cThread.Frames, cThread.Frames + cThread.Count,
                                            [&](DWORD64 aFrame) { return aFrame >= cOurBase && aFrame < ourEnd; });
        spdlog::error("HangWatchdog: thread {}{}:", cThread.Id, cInOurCode ? " (our code on its stack)" : "");
        LogFrames(cThread.Frames, cThread.Count);
    }
}

// Logs the main thread's stack when the main loop stops ticking for 10s, once per stall.
static DWORD WINAPI HangWatchdog(LPVOID apMainThread)
{
    const HANDLE cMainThread = apMainThread;
    uint64_t lastTicks = 0;
    int stalledSeconds = 0;
    while (!s_watchdogStop)
    {
        Sleep(1000);
        const uint64_t cTicks = s_mainLoopTicks.load(std::memory_order_relaxed);
        stalledSeconds = cTicks == lastTicks ? stalledSeconds + 1 : 0;
        lastTicks = cTicks;
        if (stalledSeconds != 10)
            continue;

        if (SuspendThread(cMainThread) == static_cast<DWORD>(-1))
            continue;
        // Nothing that can take a lock (logging, allocation, loader) while it is suspended.
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        DWORD64 frames[48];
        int frameCount = 0;
        if (GetThreadContext(cMainThread, &context))
            frameCount = CaptureBacktrace(context, frames, 48);
        ResumeThread(cMainThread);

        spdlog::error("HangWatchdog: main loop stalled for 10s after {} ticks", cTicks);
        LogFrames(frames, frameCount);
        LogOtherThreadsInOurCode(GetThreadId(cMainThread));
        if (auto* pDiagnostic = s_hangDiagnostic.load())
            pDiagnostic();
        spdlog::default_logger()->flush();
    }
}

void CrashHandler::OnMainLoopTick() noexcept
{
    if (s_mainLoopTicks.fetch_add(1, std::memory_order_relaxed) != 0)
        return;

    HANDLE mainThread = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &mainThread,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    s_watchdogThread = CreateThread(nullptr, 0, &HangWatchdog, mainThread, 0, nullptr);
}
