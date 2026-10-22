#include <Forms/TESQuest.h>
#include <HookSwitch.h>
#include <TESObjectREFR.h>

#include <Services/PapyrusService.h>

#include <Games/Overrides.h>
#include <Events/EventDispatcher.h>
#include <Forms/BGSBaseAlias.h>

TESObjectREFR* TESQuest::GetAliasedRef(uint32_t aAliasID) noexcept
{
    TP_THIS_FUNCTION(TGetAliasedRef, BSPointerHandle<TESObjectREFR>*, TESQuest, BSPointerHandle<TESObjectREFR>*, uint32_t);
    POINTER_SKYRIMSE(TGetAliasedRef, getAliasedRef, 25066);

    BSPointerHandle<TESObjectREFR> result{};
    TiltedPhoques::ThisCall(getAliasedRef, this, &result, aAliasID);

    return TESObjectREFR::GetByHandle(result.handle.iBits);
}

TESQuest::State TESQuest::getState()
{
    if (flags >= 0)
    {
        if (unkFlags)
            return State::WaitingPromotion;
        else if (flags & 1)
            return State::Running;
        else
            return State::Stopped;
    }

    return State::WaitingForStage;
}

void TESQuest::SetCompleted(bool aCompleted)
{
    TP_THIS_FUNCTION(TSetCompleted, void, TESQuest, bool);
    POINTER_SKYRIMSE(TSetCompleted, SetCompleted, 24991);
    SetCompleted(this, aCompleted);
}

BGSBaseAlias* TESQuest::GetRefAlias(uint32_t aAliasId) noexcept
{
    TP_THIS_FUNCTION(TGetRefAlias, BGSBaseAlias*, TESQuest, uint32_t);
    POINTER_SKYRIMSE(TGetRefAlias, getRefAlias, 25042);
    return TiltedPhoques::ThisCall(getRefAlias, this, aAliasId);
}

void TESQuest::ForceRefIntoAlias(uint32_t aAliasId, TESObjectREFR* apRef) noexcept
{
    TP_THIS_FUNCTION(TForceRefIntoAlias, void, TESQuest, uint32_t, TESObjectREFR*);
    POINTER_SKYRIMSE(TForceRefIntoAlias, forceRefIntoAlias, 25052);
    TiltedPhoques::ThisCall(forceRefIntoAlias, this, aAliasId, apRef);
}

void TESQuest::ClearRefAlias(BGSBaseAlias* apAlias) noexcept
{
    TP_THIS_FUNCTION(TClearRefAlias, void, TESQuest, BGSBaseAlias*);
    POINTER_SKYRIMSE(TClearRefAlias, clearRefAlias, 25051);
    TiltedPhoques::ThisCall(clearRefAlias, this, apAlias);
}

BGSBaseAlias* TESQuest::GetAlias(uint32_t aAliasId) noexcept
{
    for (BGSBaseAlias* pAlias : aliases)
        if (pAlias && pAlias->aliasId == aAliasId)
            return pAlias;
    return nullptr;
}

BGSLocation* TESQuest::GetAliasedLocation(uint32_t aAliasId) noexcept
{
    TP_THIS_FUNCTION(TGetAliasedLocation, BGSLocation*, TESQuest, uint32_t);
    POINTER_SKYRIMSE(TGetAliasedLocation, getAliasedLocation, 25067);
    return TiltedPhoques::ThisCall(getAliasedLocation, this, aAliasId);
}

void TESQuest::ForceLocationIntoAlias(uint32_t aAliasId, BGSLocation* apLocation) noexcept
{
    TP_THIS_FUNCTION(TForceLocationIntoAlias, void, TESQuest, uint32_t, BGSLocation*);
    POINTER_SKYRIMSE(TForceLocationIntoAlias, forceLocationIntoAlias, 25054);
    TiltedPhoques::ThisCall(forceLocationIntoAlias, this, aAliasId, apLocation);
}

void TESQuest::ClearLocationAlias(BGSBaseAlias* apAlias) noexcept
{
    TP_THIS_FUNCTION(TClearLocationAlias, void, TESQuest, BGSBaseAlias*);
    POINTER_SKYRIMSE(TClearLocationAlias, clearLocationAlias, 25053);
    TiltedPhoques::ThisCall(clearLocationAlias, this, apAlias);
}

TESQuest::Stage* TESQuest::GetStage(uint16_t aIndex) noexcept
{
    for (Stage* pStage : stages)
        if (pStage && pStage->stageIndex == aIndex)
            return pStage;
    return nullptr;
}

TESQuest::Objective* TESQuest::GetObjective(uint16_t aIndex) noexcept
{
    for (Objective* pObjective : objectives)
        if (pObjective && pObjective->index == aIndex)
            return pObjective;
    return nullptr;
}

void TESQuest::SetFailed(bool aFailed) noexcept
{
    TP_THIS_FUNCTION(TSetFailed, void, TESQuest, bool);
    POINTER_SKYRIMSE(TSetFailed, setFailed, 24988);
    TiltedPhoques::ThisCall(setFailed, this, aFailed);
}

void TESQuest::Objective::SetState(uint8_t aState) noexcept
{
    TP_THIS_FUNCTION(TSetState, void, Objective, uint32_t);
    POINTER_SKYRIMSE(TSetState, setState, 23933);
    TiltedPhoques::ThisCall(setState, this, aState);
}

void TESQuest::CompleteAllObjectives()
{
    TP_THIS_FUNCTION(TCompleteAllObjectives, void, TESQuest);
    POINTER_SKYRIMSE(TCompleteAllObjectives, CompleteAll, 23231);
    CompleteAll(this);
}

void TESQuest::SetActive(bool toggle)
{
    if (toggle)
        flags |= 0x800;
    else
        flags &= 0xF7FF;
}

bool TESQuest::IsStageDone(uint16_t stageIndex)
{
    for (Stage* it : stages)
    {
        if (it->stageIndex == stageIndex)
            return it->IsDone();
    }

    return false;
}

bool TESQuest::Kill()
{
    using TSetStopped = void(TESQuest*, bool);
    POINTER_SKYRIMSE(TSetStopped, SetStopped, 24987);

    if (flags & Flags::Enabled)
    {
        unkFlags = 0;
        flags = Flags::Completed;
        MarkChanged(2);

        // SetStopped(this, false);
        return true;
    }

    return false;
}

bool TESQuest::EnsureQuestStarted(bool& success, bool force)
{
    TP_THIS_FUNCTION(TSetRunning, bool, TESQuest, bool*, bool);
    POINTER_SKYRIMSE(TSetRunning, SetRunning, 25003);
    return SetRunning(this, &success, force);
}

bool TESQuest::SetStage(uint16_t newStage)
{
    TP_THIS_FUNCTION(TSetStage, bool, TESQuest, uint16_t);
    POINTER_SKYRIMSE(TSetStage, SetStage, 25004);
    return SetStage(this, newStage);
}

void TESQuest::ScriptSetStage(uint16_t stageIndex)
{
    if (currentStage == stageIndex || IsStageDone(stageIndex))
        return;

    using Quest = TESQuest;
    PAPYRUS_FUNCTION(void, Quest, SetCurrentStageID, int);
    s_pSetCurrentStageID(this, stageIndex);
}

void TESQuest::SetStopped()
{
    flags &= 0xFFFE;
    MarkChanged(2);
}

namespace
{
// A stage item's callback (StageItemFinishedCallback): slot 1 sends TESQuestStageItemDoneEvent (25234).
struct StageItemCallback
{
    virtual void Destroy(bool aFree);
    virtual void Run();
};

// The Papyrus VM's TESQuestStageEvent sink (53987): runs the item's fragment, then its callback; an item
// without a fragment gets the callback at once.
TP_THIS_FUNCTION(TQuestStageEventSink, uint32_t, void, const TESQuestStageEvent*, void*);
TQuestStageEventSink* RealQuestStageEventSink = nullptr;

// Set while SetStageWithoutFragments runs: the stage's events are sent on this thread (25260).
thread_local bool s_skipFragments = false;

uint32_t TP_MAKE_THISCALL(HookQuestStageEventSink, void, const TESQuestStageEvent* apEvent, void* apSource)
{
    if (s_skipFragments && apEvent)
    {
        if (apEvent->callback)
            static_cast<StageItemCallback*>(apEvent->callback)->Run();
        return 0; // BSEventNotifyControl::kContinue
    }
    return TiltedPhoques::ThisCall(RealQuestStageEventSink, apThis, apEvent, apSource);
}
} // namespace

bool TESQuest::SetStageWithoutFragments(uint16_t aStage) noexcept
{
    s_skipFragments = true;
    const bool cSet = SetStage(aStage);
    s_skipFragments = false;
    return cSet;
}

static TiltedPhoques::Initializer s_questInitHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TQuestStageEventSink, s_questStageEventSink, 53987);
        RealQuestStageEventSink = s_questStageEventSink.Get();
        TP_HOOK(&RealQuestStageEventSink, HookQuestStageEventSink);
    });
