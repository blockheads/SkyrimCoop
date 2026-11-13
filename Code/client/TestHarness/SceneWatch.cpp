// Who stops a scene (docs/re/systems/crime.md, "The steal warning in a scene"): each call of BGSScene's stop (24140,
// read in IDA: StartAlarm, quest stops, Papyrus Scene.Stop, a phase's end) with the scene and its call stack.
#include "SceneWatch.h"
#include "Commands.h"

#include <Forms/TESForm.h>
#include <MinHook.h>
#include <VersionDb.h>

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace TestHarness::SceneWatch
{
namespace
{
using TStop = void(TESForm*, bool);
TStop* RealStop{};

constexpr size_t cMaxEvents = 256;
constexpr USHORT cStackDepth = 24;

struct Event
{
    int64_t Time{};
    uint32_t Scene{};
    uint32_t Thread{};
    std::array<void*, cStackDepth> Stack{};
    USHORT Depth{};
};

std::mutex s_mutex;
std::atomic<bool> s_watching{};
std::vector<Event> s_events;

void HookStop(TESForm* apScene, bool aArgument)
{
    if (s_watching.load(std::memory_order_relaxed))
    {
        Event event{.Time = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count(),
                    .Scene = apScene ? apScene->formID : 0,
                    .Thread = GetCurrentThreadId()};
        event.Depth = RtlCaptureStackBackTrace(1, cStackDepth, event.Stack.data(), nullptr);
        std::scoped_lock _{s_mutex};
        if (s_events.size() < cMaxEvents)
            s_events.push_back(event);
    }
    RealStop(apScene, aArgument);
}

bool Install()
{
    static bool s_installed = false;
    if (s_installed)
        return true;
    VersionDbPtr<void> stop(24140);
    if (MH_CreateHook(stop.GetPtr(), reinterpret_cast<void*>(&HookStop), reinterpret_cast<void**>(&RealStop)) !=
            MH_OK ||
        MH_QueueEnableHook(stop.GetPtr()) != MH_OK || MH_ApplyQueued() != MH_OK)
        return false;
    s_installed = true;
    return true;
}

// Each frame as "<module>+0x<offset>", named on the game thread.
nlohmann::json StackJson(const Event& acEvent)
{
    auto stack = nlohmann::json::array();
    for (USHORT i = 0; i < acEvent.Depth; ++i)
    {
        HMODULE module{};
        char name[MAX_PATH]{};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(acEvent.Stack[i]), &module) &&
            GetModuleFileNameA(module, name, MAX_PATH))
        {
            std::string file = name;
            file = file.substr(file.find_last_of("\\/") + 1);
            stack.push_back(
                std::format("{}+0x{:X}", file,
                            reinterpret_cast<uintptr_t>(acEvent.Stack[i]) - reinterpret_cast<uintptr_t>(module)));
        }
        else
            stack.push_back(std::format("0x{:X}", reinterpret_cast<uintptr_t>(acEvent.Stack[i])));
    }
    return stack;
}

// {"cmd": "scene_watch", "start": true}: starts; {} takes the stops so far; {"stop": true} takes them and ends.
nlohmann::json Command(Harness&, const nlohmann::json& acRequest)
{
    if (acRequest.value("start", false))
    {
        if (!Install())
            return {{"ok", false}, {"error", "could not hook 24140"}};
        s_watching = true;
    }
    std::vector<Event> taken;
    {
        std::scoped_lock _{s_mutex};
        taken = std::exchange(s_events, {});
        if (acRequest.value("stop", false))
            s_watching = false;
    }
    auto events = nlohmann::json::array();
    for (const Event& cEvent : taken)
        events.push_back(
            {{"t", cEvent.Time}, {"scene", cEvent.Scene}, {"thread", cEvent.Thread}, {"stack", StackJson(cEvent)}});
    return {{"ok", true}, {"stops", events}};
}

constexpr TestHarness::Command cCommands[] = {{"scene_watch", true, &Command}};
} // namespace

std::span<const TestHarness::Command> Commands()
{
    return cCommands;
}
} // namespace TestHarness::SceneWatch
