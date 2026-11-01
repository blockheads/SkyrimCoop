#include "Commands.h"
#include "Harness.h"

#include <CrashHandler.h>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <fstream>
#include <map>
#include <thread>
#include <vector>

// Whole-process stack sampling for coop_test.py profile --stacks (docs/design/testing.md, Profiling): engine code and
// ours, under Wine as on Windows, no admin. Every interval the busiest threads are suspended in turn and unwound
// (CrashHandler::Backtrace); nothing allocates while one is suspended.
namespace TestHarness
{
namespace
{
constexpr int cMaxFrames = 64;
constexpr size_t cWatched = 8; // the main thread and the busiest others, chosen again every second

struct Watched
{
    DWORD Id;
    HANDLE Handle;
    uint64_t Cpu;     // kernel + user, 100 ns, at the last choice
    uint64_t CpuUsed; // over the last second
    uint64_t Samples;
};

struct Captured
{
    DWORD Thread;
    int Count;
    DWORD64 Frames[cMaxFrames];
};

std::atomic<bool> s_running{false};
std::atomic<bool> s_stop{false};
std::atomic<uint64_t> s_finished{0}; // runs written
std::thread s_thread;
std::filesystem::path s_lastFile;

uint64_t CpuOf(HANDLE aThread) noexcept
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(aThread, &created, &exited, &kernel, &user))
        return 0;
    const auto cTicks = [](const FILETIME& acTime) { return static_cast<uint64_t>(acTime.dwHighDateTime) << 32 | acTime.dwLowDateTime; };
    return cTicks(kernel) + cTicks(user);
}

// Every thread of this process but the sampler's, with what each used since the last call.
std::vector<Watched> Threads(const std::vector<Watched>& acPrevious) noexcept
{
    std::vector<Watched> threads;
    const HANDLE cSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (cSnapshot == INVALID_HANDLE_VALUE)
        return threads;
    THREADENTRY32 entry{sizeof(entry)};
    for (BOOL more = Thread32First(cSnapshot, &entry); more; more = Thread32Next(cSnapshot, &entry))
    {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId())
            continue;
        const HANDLE cHandle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!cHandle)
            continue;
        const uint64_t cCpu = CpuOf(cHandle);
        const auto cOld = std::find_if(acPrevious.begin(), acPrevious.end(), [&](const Watched& acWatched) { return acWatched.Id == entry.th32ThreadID; });
        threads.push_back({entry.th32ThreadID, cHandle, cCpu, cOld != acPrevious.end() ? cCpu - cOld->Cpu : 0, cOld != acPrevious.end() ? cOld->Samples : 0});
    }
    CloseHandle(cSnapshot);
    return threads;
}

void Run(std::filesystem::path aFile, double aSeconds, DWORD aIntervalMs, DWORD aMainThread)
{
    std::map<std::pair<DWORD, std::vector<DWORD64>>, uint32_t> stacks;
    std::vector<Watched> all = Threads({});
    std::vector<Captured> sweep(cWatched);
    const uint64_t cStart = GetTickCount64();
    uint64_t nextChoice = 0;
    std::vector<Watched*> watched;
    uint64_t sweeps = 0;
    while (!s_stop && GetTickCount64() - cStart < static_cast<uint64_t>(aSeconds * 1000))
    {
        if (GetTickCount64() >= nextChoice)
        {
            auto fresh = Threads(all);
            for (const Watched& cOld : all)
                CloseHandle(cOld.Handle);
            all = std::move(fresh);
            watched.clear();
            for (Watched& thread : all)
                watched.push_back(&thread);
            // The main thread first, then the most CPU over the last second.
            std::sort(watched.begin(), watched.end(), [&](const Watched* a, const Watched* b) {
                return (a->Id == aMainThread) != (b->Id == aMainThread) ? a->Id == aMainThread : a->CpuUsed > b->CpuUsed;
            });
            watched.resize(std::min(watched.size(), cWatched));
            nextChoice = GetTickCount64() + 1000;
        }

        size_t captured = 0;
        for (Watched* pThread : watched)
        {
            if (SuspendThread(pThread->Handle) == static_cast<DWORD>(-1))
                continue;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            Captured& slot = sweep[captured];
            slot.Thread = pThread->Id;
            slot.Count = GetThreadContext(pThread->Handle, &context) ? CrashHandler::Backtrace(context, slot.Frames, cMaxFrames) : 0;
            ResumeThread(pThread->Handle);
            if (slot.Count)
            {
                ++captured;
                ++pThread->Samples;
            }
        }
        // Every thread runs again: allocating is safe.
        for (size_t i = 0; i < captured; ++i)
            ++stacks[{sweep[i].Thread, std::vector<DWORD64>(sweep[i].Frames, sweep[i].Frames + sweep[i].Count)}];
        ++sweeps;
        Sleep(aIntervalMs);
    }
    const uint64_t cElapsed = GetTickCount64() - cStart;

    std::ofstream out(aFile, std::ios::trunc);
    out << "# SkyrimCoop stack samples v1\n";
    out << std::format("duration_ms {} interval_ms {} sweeps {} main_thread {}\n", cElapsed, aIntervalMs, sweeps, aMainThread);
    HMODULE modules[1024];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        for (size_t i = 0; i < std::min<size_t>(needed / sizeof(HMODULE), 1024); ++i)
        {
            MODULEINFO info{};
            wchar_t path[MAX_PATH]{};
            GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info));
            GetModuleFileNameW(modules[i], path, MAX_PATH);
            char narrow[MAX_PATH * 3]{};
            WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
            out << std::format("module {:x} {:x} {}\n", reinterpret_cast<uintptr_t>(info.lpBaseOfDll), info.SizeOfImage, narrow);
        }
    for (const Watched& cThread : all)
    {
        if (cThread.Samples)
        {
            // Looked up: older Wine has no GetThreadDescription, and an import would keep the DLL from loading.
            using TDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);
            static const auto s_description = reinterpret_cast<TDescription>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
            PWSTR pName = nullptr;
            char name[256] = "";
            if (s_description && SUCCEEDED(s_description(cThread.Handle, &pName)) && pName)
            {
                WideCharToMultiByte(CP_UTF8, 0, pName, -1, name, sizeof(name), nullptr, nullptr);
                LocalFree(pName);
            }
            out << std::format("thread {} samples {} cpu_ms {} name {}\n", cThread.Id, cThread.Samples, cThread.Cpu / 10000, name);
        }
        CloseHandle(cThread.Handle);
    }
    for (const auto& [cKey, cCount] : stacks)
    {
        out << std::format("stack {} {}", cKey.first, cCount);
        for (const DWORD64 cFrame : cKey.second)
            out << std::format(" {:x}", cFrame);
        out << '\n';
    }
    out.close();
    s_lastFile = aFile;
    ++s_finished;
    s_running = false;
}

// {"cmd": "sample", "seconds": 20, "interval_ms": 1}: starts sampling (one run at a time); sample_result says when
// its file (in the control folder) is written.
nlohmann::json Start(Harness& aHarness, const nlohmann::json& acRequest)
{
    if (s_running.exchange(true))
        return Error("a sampling run is still going");
    if (s_thread.joinable())
        s_thread.join();
    s_stop = false;
    const auto cFile = aHarness.Files.Directory() / std::format("samples-{}.txt", GetTickCount64());
    const double cSeconds = std::clamp(acRequest.value("seconds", 20.0), 0.1, 600.0);
    const DWORD cInterval = std::clamp(acRequest.value("interval_ms", 1u), 0u, 1000u);
    // Commands run on the main thread (TestHarness::Poll).
    s_thread = std::thread(Run, cFile, cSeconds, cInterval, GetCurrentThreadId());
    return {{"ok", true}, {"file", cFile.string()}, {"run", s_finished.load() + 1}};
}

nlohmann::json Result(Harness&, const nlohmann::json&)
{
    return {{"ok", true}, {"running", s_running.load()}, {"finished", s_finished.load()}, {"file", s_lastFile.string()}};
}

constexpr Command cCommands[] = {{"sample", false, &Start}, {"sample_result", false, &Result}};
} // namespace

std::span<const Command> SamplerCommands()
{
    return cCommands;
}

void StopSampler() noexcept
{
    s_stop = true;
    if (s_thread.joinable())
        s_thread.join();
}
} // namespace TestHarness
