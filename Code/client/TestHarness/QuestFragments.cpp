#include <HookSwitch.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Forms/TESQuest.h>
#include "Commands.h"

#include <map>
#include <mutex>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

// Counts the quest stage fragments the Papyrus VM is about to run, per quest: the VM's TESQuestStageEvent
// sink (53987) looks each up (53429) before it runs one, and skips the lookup for a stage a guest applied
// from the host (TESQuest::SetStageWithoutFragments). For the world-state scenarios: a guest runs none.
namespace TestHarness
{
namespace
{
// (fragment map, quest, stage, item, out script name, out function name): true when the item has one.
using TFindFragment = bool(void*, uint32_t, uint32_t, uint32_t, void*, void*);
TFindFragment* RealFindFragment = nullptr;

std::mutex s_lock;
struct Counts
{
    uint32_t Stages = 0, ItemsDone = 0, Starts = 0, Stops = 0, Completions = 0;
};
std::map<uint32_t, Counts> s_events;
bool s_watching = false;
std::map<uint32_t, uint32_t> s_runs; // quest form id -> fragments found to run

template <class T> struct QuestEventSink final : BSTEventSink<T>
{
    BSTEventResult OnEvent(const T* apEvent, const EventDispatcher<T>*) override
    {
        if (apEvent)
        {
            std::scoped_lock lock(s_lock);
            if constexpr (std::is_same_v<T, QuestStatusEvent>)
            {
                if (apEvent->quest && apEvent->status == 0) ++s_events[apEvent->quest->formID].Completions;
            }
            else
            {
                auto& counts = s_events[apEvent->formId];
                if constexpr (std::is_same_v<T, TESQuestStageEvent>) ++counts.Stages;
                else if constexpr (std::is_same_v<T, TESQuestStageItemDoneEvent>) ++counts.ItemsDone;
                else if (apEvent->started) ++counts.Starts;
                else ++counts.Stops;
            }
        }
        return BSTEventResult::kOk;
    }
};
QuestEventSink<TESQuestStageEvent> s_stages;
QuestEventSink<TESQuestStageItemDoneEvent> s_items;
QuestEventSink<TESQuestStartStopEvent> s_startStop;
QuestEventSink<QuestStatusEvent> s_status;

EventDispatcher<QuestStatusEvent>* StatusEvents()
{
    POINTER_SKYRIMSE(EventDispatcher<QuestStatusEvent>*(), source, 25196);
    return source.Get()();
}

bool HookFindFragment(void* apThis, uint32_t aQuestId, uint32_t aStage, uint32_t aItem, void* apScript, void* apFunction)
{
    const bool cFound = RealFindFragment(apThis, aQuestId, aStage, aItem, apScript, apFunction);
    if (cFound)
    {
        std::scoped_lock lock(s_lock);
        ++s_runs[aQuestId];
    }
    return cFound;
}

Hooks::Initializer s_questFragmentHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(53429, &RealFindFragment, HookFindFragment);
    });

// {"cmd": "quest_fragments", "quest": id}: fragments the VM went to run for that quest since the last reset.
nlohmann::json QuestFragmentsCommand(Harness&, const nlohmann::json& acRequest)
{
    std::scoped_lock lock(s_lock);
    const auto it = s_runs.find(acRequest.value("quest", 0u));
    const auto& counts = s_events[acRequest.value("quest", 0u)];
    return {{"ok", true}, {"runs", it == s_runs.end() ? 0u : it->second}, {"stages", counts.Stages},
            {"items_done", counts.ItemsDone}, {"starts", counts.Starts}, {"stops", counts.Stops}, {"completions", counts.Completions}};
}

nlohmann::json QuestFragmentsResetCommand(Harness&, const nlohmann::json&)
{
    if (!s_watching)
    {
        if (auto* pEvents = EventDispatcherManager::Get())
        {
            pEvents->questStageEvent.RegisterSink(&s_stages);
            pEvents->questStageItemDoneEvent.RegisterSink(&s_items);
            pEvents->questStartStopEvent.RegisterSink(&s_startStop);
            StatusEvents()->RegisterSink(&s_status);
            s_watching = true;
        }
    }
    std::scoped_lock lock(s_lock);
    s_events.clear();
    s_runs.clear();
    return Ok();
}

constexpr Command cCommands[] = {
    {"quest_fragments", false, &QuestFragmentsCommand},
    {"quest_fragments_reset", false, &QuestFragmentsResetCommand},
};
} // namespace

void StopQuestTrace() noexcept
{
    if (!s_watching) return;
    if (auto* pEvents = EventDispatcherManager::Get())
    {
        pEvents->questStageEvent.UnRegisterSink(&s_stages);
        pEvents->questStageItemDoneEvent.UnRegisterSink(&s_items);
        pEvents->questStartStopEvent.UnRegisterSink(&s_startStop);
        StatusEvents()->UnRegisterSink(&s_status);
    }
    s_watching = false;
}

std::span<const Command> QuestFragmentsCommands()
{
    return cCommands;
}
} // namespace TestHarness
