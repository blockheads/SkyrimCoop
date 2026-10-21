#include <CellEvents.h>

#include <Events/CellLoadedEvent.h>
#include <Events/EventDispatcher.h>
#include <Forms/TESObjectCELL.h>
#include <Games/Events.h>
#include <World.h>

namespace CellEvents
{
namespace
{
// Queued: handled on the next update, where the cell's references are read.
struct Sink final : BSTEventSink<TESCellFullyLoadedEvent>
{
    BSTEventResult OnEvent(const TESCellFullyLoadedEvent* apEvent, const EventDispatcher<TESCellFullyLoadedEvent>*) override
    {
        if (apEvent && apEvent->cell)
            World::Get().GetRunner().Trigger(CellLoadedEvent{apEvent->cell->formID});
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
        pEvents->cellFullyLoadedEvent.RegisterSink(&s_sink);
}

void Stop() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (pEvents && s_users && --s_users == 0)
        pEvents->cellFullyLoadedEvent.UnRegisterSink(&s_sink);
}
} // namespace CellEvents
