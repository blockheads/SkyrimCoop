#include <ContainerEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Games/Overrides.h>
#include <World.h>

namespace ContainerEvents
{
namespace
{
// Runs where the change happens (any thread, inside the engine's add or remove): the event is queued.
struct Sink final : BSTEventSink<TESContainerChangedEvent>
{
    BSTEventResult OnEvent(const TESContainerChangedEvent* apEvent, const EventDispatcher<TESContainerChangedEvent>*) override
    {
        if (!apEvent || ScopedInventoryOverride::IsOverriden())
            return BSTEventResult::kOk;
        for (const uint32_t cContainer : {apEvent->oldContainerID, apEvent->newContainerID})
            if (cContainer)
                World::Get().GetRunner().Trigger(ContainerChangeEvent{cContainer, apEvent->baseObjectID});
        return BSTEventResult::kOk;
    }
};

Sink s_sink;
uint32_t s_users = 0;
} // namespace

void Start() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (pEvents && s_users++ == 0)
        pEvents->containerChangedEvent.RegisterSink(&s_sink);
}

void Stop() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (pEvents && s_users && --s_users == 0)
        pEvents->containerChangedEvent.UnRegisterSink(&s_sink);
}
} // namespace ContainerEvents
