#include <LoadGameEvents.h>

#include <Events/EventDispatcher.h>
#include <Events/GameLoadedEvent.h>
#include <Games/Events.h>
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
