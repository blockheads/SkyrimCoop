#include <LoadGameEvents.h>

#include <Events/EventDispatcher.h>
#include <Events/GameLoadedEvent.h>
#include <Events/NewGameEvent.h>
#include <Games/Events.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <World.h>

namespace LoadGameEvents
{
namespace
{
struct LoadSink final : BSTEventSink<TESLoadGameEvent>
{
    BSTEventResult OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*) override
    {
        World::Get().GetRunner().Trigger(GameLoadedEvent{});
        return BSTEventResult::kOk;
    }
};

LoadSink s_load;
bool s_registered = false;

// NewGame_Enter (52118, SKSE's NewGame hook): the main menu's New, as its handler (MainMenu::Setup) calls it. It
// starts the intro quest; the arguments pass through untouched.
using TNewGameEnter = uint64_t(void*, void*, void*, void*);
TNewGameEnter* RealNewGameEnter = nullptr;

uint64_t HookNewGameEnter(void* apA, void* apB, void* apC, void* apD)
{
    const uint64_t cResult = RealNewGameEnter(apA, apB, apC, apD);
    World::Get().GetRunner().Trigger(NewGameEvent{});
    return cResult;
}

Hooks::Initializer s_newGameHook(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(52118, &RealNewGameEnter, HookNewGameEnter);
    });
} // namespace

void Start() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || s_registered)
        return;
    pEvents->loadGameEvent.RegisterSink(&s_load);
    s_registered = true;
}

// Before a hot reload unloads this code.
void Stop() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || !s_registered)
        return;
    pEvents->loadGameEvent.UnRegisterSink(&s_load);
    s_registered = false;
}
} // namespace LoadGameEvents
