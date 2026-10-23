#include <Misc/BGSStoryManager.h>

#include <HookSwitch.h>

#include <atomic>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace
{
std::atomic<bool> s_picksQuests{true};

// BGSStoryManager::ProcessEvent (32372): walks the story tree for one event and adds the quests it starts to
// apStartQuests; BGSStoryTeller::Update (32483) then starts them and ends the event unless it timed out.
using TProcessEvent = uint64_t(void* apThis, const void* apEvent, void* apTimer, bool* apTimedOut, bool* apUnk, bool* apUnk2, void* apStartQuests);
TProcessEvent* RealProcessEvent = nullptr;

uint64_t HookProcessEvent(void* apThis, const void* apEvent, void* apTimer, bool* apTimedOut, bool* apUnk, bool* apUnk2, void* apStartQuests)
{
    if (s_picksQuests.load(std::memory_order_relaxed))
        return RealProcessEvent(apThis, apEvent, apTimer, apTimedOut, apUnk, apUnk2, apStartQuests);
    *apTimedOut = false;
    return 0;
}

Hooks::Initializer s_storyManagerHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(32372, &RealProcessEvent, HookProcessEvent);
    });
} // namespace

void BGSStoryManager::SetPicksQuests(bool aPicks) noexcept
{
    s_picksQuests.store(aPicks, std::memory_order_relaxed);
}
