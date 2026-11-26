#include <Misc/BGSStoryManager.h>

#include <HookSwitch.h>

#include <atomic>
#include <Components/TESCondition.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Hooks/Memory.h>
#include <Misc/JudgedFor.h>
#include <Misc/StoryEvents.h>
#include <Actor.h>
#include <VersionDb.h>

namespace
{
std::atomic<bool> s_picksQuests{true};
// BGSStoryManager::ProcessEvent (32372): walks the story tree for one event and adds the quests it starts to
// apStartQuests; BGSStoryTeller::Update (32483) then starts them and ends the event unless it timed out.
using TProcessEvent = uint64_t(void* apThis, const BGSStoryEvent* apEvent, void* apTimer, bool* apTimedOut, bool* apUnk, bool* apUnk2, void* apStartQuests);
TProcessEvent* RealProcessEvent = nullptr;

uint64_t HookProcessEvent(void* apThis, const BGSStoryEvent* apEvent, void* apTimer, bool* apTimedOut, bool* apUnk, bool* apUnk2, void* apStartQuests)
{
    const auto cVerdict = s_picksQuests.load(std::memory_order_relaxed) && apEvent ? StoryEvents::Judge(*apEvent) : StoryEvents::Verdict{false};
    if (!cVerdict.Process)
    {
        *apTimedOut = false;
        if (apEvent)
            StoryEvents::Done(apEvent->id);
        return 0;
    }
    // Judged for a guest's character (StoryEvents::Judge), its conditions too.
    const JudgedFor::Scope cJudged(cVerdict.pJudged);
    const uint64_t cResult = RealProcessEvent(apThis, apEvent, apTimer, apTimedOut, apUnk, apUnk2, apStartQuests);
    if (!*apTimedOut)
        StoryEvents::Done(apEvent->id);
    return cResult;
}

// ConditionCheckParams' constructor (29871: params, subject, target, owner form), at the node visitor's (32429) and
// the quest visitor's (32430) call, both (g_thePlayer, g_thePlayer): the judged character in its place.
using TMakeParams = ConditionCheckParams*(ConditionCheckParams*, TESObjectREFR*, TESObjectREFR*, TESForm*);
TMakeParams* RealNodeParams = nullptr;
TMakeParams* RealQuestParams = nullptr;

ConditionCheckParams* NodeParams(ConditionCheckParams* apParams, TESObjectREFR* apSubject, TESObjectREFR* apTarget, TESForm* apOwner)
{
    TESObjectREFR* pJudged = JudgedFor::Get();
    return RealNodeParams(apParams, pJudged ? pJudged : apSubject, pJudged ? pJudged : apTarget, apOwner);
}

ConditionCheckParams* QuestParams(ConditionCheckParams* apParams, TESObjectREFR* apSubject, TESObjectREFR* apTarget, TESForm* apOwner)
{
    TESObjectREFR* pJudged = JudgedFor::Get();
    return RealQuestParams(apParams, pJudged ? pJudged : apSubject, pJudged ? pJudged : apTarget, apOwner);
}

// The call at aOffset into its visitor calls 29871, or it is left alone.
bool IsParamsCall(uint8_t* apVisitor, uint32_t aOffset)
{
    POINTER_SKYRIMSE(uint8_t, s_makeParams, 29871);
    const uint8_t* pCall = apVisitor + aOffset;
    if (*pCall == 0xE8 && pCall + 5 + *reinterpret_cast<const int32_t*>(pCall + 1) == s_makeParams.Get())
        return true;
    spdlog::error("BGSStoryManager: +{:X} is not the condition parameters' call: a guest's story events are judged for this game's player", aOffset);
    return false;
}

Hooks::Initializer s_storyManagerHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(32372, &RealProcessEvent, HookProcessEvent);
        POINTER_SKYRIMSE(uint8_t, s_nodeVisitor, 32429);
        POINTER_SKYRIMSE(uint8_t, s_questVisitor, 32430);
        if (IsParamsCall(s_nodeVisitor.Get(), 0x19D))
            Hooks::SwapCall(s_nodeVisitor.Get() + 0x19D, RealNodeParams, &NodeParams);
        if (IsParamsCall(s_questVisitor.Get(), 0x3F))
            Hooks::SwapCall(s_questVisitor.Get() + 0x3F, RealQuestParams, &QuestParams);
    });
} // namespace

void BGSStoryManager::SetPicksQuests(bool aPicks) noexcept
{
    s_picksQuests.store(aPicks, std::memory_order_relaxed);
}
