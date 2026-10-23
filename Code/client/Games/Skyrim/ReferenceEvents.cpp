#include <ReferenceEvents.h>

#include <Events/EventDispatcher.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/Events.h>
#include <World.h>

namespace ReferenceEvents
{
namespace
{
struct LoadedSink final : BSTEventSink<TESObjectLoadedEvent>
{
    BSTEventResult OnEvent(const TESObjectLoadedEvent* apEvent, const EventDispatcher<TESObjectLoadedEvent>*) override
    {
        if (apEvent && apEvent->loaded)
            World::Get().GetRunner().Trigger(ReferenceLoadedEvent{apEvent->formID});
        return BSTEventResult::kOk;
    }
};

LoadedSink s_loaded;
uint32_t s_users = 0;
} // namespace

void Start() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || s_users++ != 0)
        return;
    pEvents->objectLoadedEvent.RegisterSink(&s_loaded);
}

void Stop() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || !s_users || --s_users != 0)
        return;
    pEvents->objectLoadedEvent.UnRegisterSink(&s_loaded);
}
} // namespace ReferenceEvents
