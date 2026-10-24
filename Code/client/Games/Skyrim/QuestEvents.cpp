#include <QuestEvents.h>

#include <Events/EventDispatcher.h>
#include <Forms/FormChanges.h>
#include <Forms/TESQuest.h>
#include <Games/Events.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

#include <atomic>

namespace QuestEvents
{
namespace
{
QuestChanges s_changes;
std::atomic<bool> s_active = false;

void Raise(const TESQuest* apQuest)
{
    if (s_active && apQuest)
        s_changes.Quest(apQuest->formID);
}

void OnFormChange(const TESForm& acForm, uint32_t, bool, bool) noexcept
{
    if (!s_active)
        return;
    if (acForm.formType == FormType::Quest)
        s_changes.Quest(acForm.formID);
    else
        s_changes.Reference(acForm.formID);
}

template <class T> struct QuestSink final : BSTEventSink<T>
{
    BSTEventResult OnEvent(const T* apEvent, const EventDispatcher<T>*) override
    {
        if (s_active && apEvent)
            s_changes.Quest(apEvent->formId);
        return BSTEventResult::kOk;
    }
};

// ObjectiveState::Event (CommonLibSSE-NG): raised before SetState writes, read on the host's next update.
struct ObjectiveEvent
{
    TESQuest::Objective* objective;
    uint32_t oldState;
    uint32_t newState;
};
static_assert(sizeof(ObjectiveEvent) == 0x10 && offsetof(ObjectiveEvent, newState) == 0xC);

struct ObjectiveSink final : BSTEventSink<ObjectiveEvent>
{
    BSTEventResult OnEvent(const ObjectiveEvent* apEvent, const EventDispatcher<ObjectiveEvent>*) override
    {
        if (apEvent && apEvent->objective)
            Raise(apEvent->objective->parent);
        return BSTEventResult::kOk;
    }
};

struct MoveSink final : BSTEventSink<TESMoveAttachDetachEvent>
{
    BSTEventResult OnEvent(const TESMoveAttachDetachEvent* apEvent,
                           const EventDispatcher<TESMoveAttachDetachEvent>*) override
    {
        if (s_active && apEvent && apEvent->movedRef)
            s_changes.Reference(apEvent->movedRef->formID);
        return BSTEventResult::kOk;
    }
};

QuestSink<TESQuestStageEvent> s_stage;
QuestSink<TESQuestStartStopEvent> s_startStop;
ObjectiveSink s_objective;
MoveSink s_move;

EventDispatcher<ObjectiveEvent>* Objectives()
{
    POINTER_SKYRIMSE(EventDispatcher<ObjectiveEvent> * (), getSource, 23951);
    return getSource.Get()();
}

using TReset = void(TESQuest*);
using TClear = void(TESQuest*, BGSBaseAlias*);
using TForceRef = void(TESQuest*, uint32_t, TESObjectREFR*);
using TForceLocation = void(TESQuest*, uint32_t, BGSLocation*);
TReset* RealReset = nullptr;
TClear* RealClearRef = nullptr;
TClear* RealClearLocation = nullptr;
TForceRef* RealForceRef = nullptr;
TForceLocation* RealForceLocation = nullptr;

void HookReset(TESQuest* apQuest)
{
    RealReset(apQuest);
    Raise(apQuest);
}

void HookClearRef(TESQuest* apQuest, BGSBaseAlias* apAlias)
{
    RealClearRef(apQuest, apAlias);
    Raise(apQuest);
}

void HookClearLocation(TESQuest* apQuest, BGSBaseAlias* apAlias)
{
    RealClearLocation(apQuest, apAlias);
    Raise(apQuest);
}

void HookForceRef(TESQuest* apQuest, uint32_t aAlias, TESObjectREFR* apRef)
{
    RealForceRef(apQuest, aAlias, apRef);
    Raise(apQuest);
}

void HookForceLocation(TESQuest* apQuest, uint32_t aAlias, BGSLocation* apLocation)
{
    RealForceLocation(apQuest, aAlias, apLocation);
    Raise(apQuest);
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(25014, &RealReset, HookReset);
    HOOK_ID(25051, &RealClearRef, HookClearRef);
    HOOK_ID(25052, &RealForceRef, HookForceRef);
    HOOK_ID(25053, &RealClearLocation, HookClearLocation);
    HOOK_ID(25054, &RealForceLocation, HookForceLocation);
});
} // namespace

QuestChanges& Changes() noexcept
{
    return s_changes;
}

void Start() noexcept
{
    s_changes.Clear();
    s_active = true;
    if (!FormChanges::Watch(&OnFormChange))
        spdlog::error("QuestEvents: no slot to watch quest flags and resets");
    if (auto* pEvents = EventDispatcherManager::Get())
    {
        pEvents->questStageEvent.RegisterSink(&s_stage);
        pEvents->questStartStopEvent.RegisterSink(&s_startStop);
        pEvents->moveAttachDetachEvent.RegisterSink(&s_move);
    }
    Objectives()->RegisterSink(&s_objective);
}

void Stop() noexcept
{
    s_active = false;
    FormChanges::Unwatch(&OnFormChange);
    if (auto* pEvents = EventDispatcherManager::Get())
    {
        pEvents->questStageEvent.UnRegisterSink(&s_stage);
        pEvents->questStartStopEvent.UnRegisterSink(&s_startStop);
        pEvents->moveAttachDetachEvent.UnRegisterSink(&s_move);
    }
    Objectives()->UnRegisterSink(&s_objective);
    s_changes.Clear();
}
} // namespace QuestEvents
