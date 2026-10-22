#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>
#include "Commands.h"

#include <map>
#include <mutex>

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
std::map<uint32_t, uint32_t> s_runs; // quest form id -> fragments found to run

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
        POINTER_SKYRIMSE(TFindFragment, s_findFragment, 53429);
        RealFindFragment = s_findFragment.Get();
        HOOK(&RealFindFragment, HookFindFragment);
    });

// {"cmd": "quest_fragments", "quest": id}: fragments the VM went to run for that quest since the last reset.
nlohmann::json QuestFragmentsCommand(Harness&, const nlohmann::json& acRequest)
{
    std::scoped_lock lock(s_lock);
    const auto it = s_runs.find(acRequest.value("quest", 0u));
    return {{"ok", true}, {"runs", it == s_runs.end() ? 0u : it->second}};
}

nlohmann::json QuestFragmentsResetCommand(Harness&, const nlohmann::json&)
{
    std::scoped_lock lock(s_lock);
    s_runs.clear();
    return Ok();
}

constexpr Command cCommands[] = {
    {"quest_fragments", false, &QuestFragmentsCommand},
    {"quest_fragments_reset", false, &QuestFragmentsResetCommand},
};
} // namespace

std::span<const Command> QuestFragmentsCommands()
{
    return cCommands;
}
} // namespace TestHarness
