#include <TiltedOnlinePCH.h>
#include "EngineEvents.h"

#include <Games/Events.h>
#include <TESObjectREFR.h>

#include <deque>
#include <mutex>

namespace TestHarness
{
namespace
{
constexpr size_t cKept = 512;

std::mutex s_lock;
std::deque<nlohmann::json> s_events;
uint64_t s_next = 0;

// Sent where the engine raises them, not only on the game thread.
void Record(nlohmann::json aEvent)
{
    std::scoped_lock _{s_lock};
    aEvent["seq"] = s_next++;
    s_events.push_back(std::move(aEvent));
    if (s_events.size() > cKept)
        s_events.pop_front();
}

uint32_t IdOf(const TESObjectREFR* apReference) noexcept
{
    return apReference ? apReference->formID : 0;
}

struct Sinks final : BSTEventSink<TESSpellCastEvent>,
                     BSTEventSink<TESMagicEffectApplyEvent>,
                     BSTEventSink<TESActiveEffectApplyRemove>,
                     BSTEventSink<TESQuestStartStopEvent>,
                     BSTEventSink<TESEquipEvent>,
                     BSTEventSink<TESHitEvent>,
                     BSTEventSink<BGSEventProcessedEvent>,
                     BSTEventSink<TESActivateEvent>,
                     BSTEventSink<TESLockChangedEvent>,
                     BSTEventSink<TESOpenCloseEvent>,
                     BSTEventSink<TESMoveAttachDetachEvent>,
                     BSTEventSink<TESObjectLoadedEvent>,
                     BSTEventSink<TESContainerChangedEvent>
{
    BSTEventResult OnEvent(const TESSpellCastEvent* apEvent, const EventDispatcher<TESSpellCastEvent>*) override
    {
        if (apEvent)
            Record({{"kind", "spell_cast"}, {"caster", IdOf(apEvent->object)}, {"spell", apEvent->spell}});
        return BSTEventResult::kOk;
    }
    BSTEventResult OnEvent(const TESMagicEffectApplyEvent* apEvent, const EventDispatcher<TESMagicEffectApplyEvent>*) override
    {
        if (apEvent)
            Record({{"kind", "magic_effect_apply"}, {"target", IdOf(apEvent->hTarget)}, {"caster", IdOf(apEvent->hCaster)},
                    {"effect", apEvent->uiMagicEffectFormID}});
        return BSTEventResult::kOk;
    }
    BSTEventResult OnEvent(const TESActiveEffectApplyRemove* apEvent, const EventDispatcher<TESActiveEffectApplyRemove>*) override
    {
        if (apEvent)
            Record({{"kind", apEvent->bIsApplied ? "effect_start" : "effect_finish"}, {"target", IdOf(apEvent->hTarget)},
                    {"caster", IdOf(apEvent->hCaster)}, {"unique_id", apEvent->usActiveEffectUniqueID}});
        return BSTEventResult::kOk;
    }
    BSTEventResult OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*) override
    {
        if (apEvent)
            Record({{"kind", apEvent->started ? "quest_start" : "quest_stop"}, {"quest", apEvent->formId}, {"failed", apEvent->failed}});
        return BSTEventResult::kOk;
    }
    BSTEventResult OnEvent(const BGSEventProcessedEvent* apEvent, const EventDispatcher<BGSEventProcessedEvent>*) override
    {
        if (apEvent)
            Record({{"kind", "story_event_processed"}, {"event", apEvent->eventId}, {"started_quest", apEvent->startedQuest}});
        return BSTEventResult::kOk;
    }
    BSTEventResult OnEvent(const TESHitEvent* apEvent, const EventDispatcher<TESHitEvent>*) override
    {
        if (apEvent)
            Record({{"kind", "hit"}, {"target", IdOf(apEvent->hit)}, {"cause", IdOf(apEvent->hitter)}, {"source", apEvent->source},
                    {"projectile", apEvent->projectile}});
        return BSTEventResult::kOk;
    }
    // The world's objects' events (Papyrus OnActivate, OnLockStateChanged, OnOpen/OnClose, OnCellAttach/Detach,
    // OnLoad/OnUnload, OnItemAdded/Removed): what applying a change raised in this game.
    BSTEventResult OnEvent(const TESActivateEvent* apEvent, const EventDispatcher<TESActivateEvent>*) override
    {
        if (apEvent)
            Record({{"kind", "activate"}, {"object", IdOf(apEvent->object)}, {"by", IdOf(apEvent->actionRef)}});
        return BSTEventResult::kOk;
    }

    BSTEventResult OnEvent(const TESLockChangedEvent* apEvent, const EventDispatcher<TESLockChangedEvent>*) override
    {
        if (apEvent)
            Record({{"kind", "lock_changed"}, {"object", IdOf(apEvent->lockedObject)}});
        return BSTEventResult::kOk;
    }

    BSTEventResult OnEvent(const TESOpenCloseEvent* apEvent, const EventDispatcher<TESOpenCloseEvent>*) override
    {
        if (apEvent)
            Record({{"kind", apEvent->opened ? "open" : "close"}, {"object", IdOf(apEvent->ref)}, {"by", IdOf(apEvent->activeRef)}});
        return BSTEventResult::kOk;
    }

    BSTEventResult OnEvent(const TESMoveAttachDetachEvent* apEvent, const EventDispatcher<TESMoveAttachDetachEvent>*) override
    {
        if (apEvent)
            Record({{"kind", apEvent->isCellAttached ? "cell_attach" : "cell_detach"}, {"object", IdOf(apEvent->movedRef)}});
        return BSTEventResult::kOk;
    }

    BSTEventResult OnEvent(const TESObjectLoadedEvent* apEvent, const EventDispatcher<TESObjectLoadedEvent>*) override
    {
        if (apEvent)
            Record({{"kind", apEvent->loaded ? "loaded" : "unloaded"}, {"object", apEvent->formID}});
        return BSTEventResult::kOk;
    }

    BSTEventResult OnEvent(const TESContainerChangedEvent* apEvent, const EventDispatcher<TESContainerChangedEvent>*) override
    {
        if (apEvent)
            Record({{"kind", "container_changed"}, {"from", apEvent->oldContainerID}, {"to", apEvent->newContainerID},
                    {"item", apEvent->baseObjectID}, {"count", apEvent->itemCount}});
        return BSTEventResult::kOk;
    }

    BSTEventResult OnEvent(const TESEquipEvent* apEvent, const EventDispatcher<TESEquipEvent>*) override
    {
        if (apEvent)
            Record({{"kind", apEvent->equipped ? "equip" : "unequip"}, {"actor", IdOf(apEvent->actor)}, {"item", apEvent->baseObject}});
        return BSTEventResult::kOk;
    }
};
Sinks s_sinks;

template <class T> void Listen(EventDispatcher<T>& aSource, bool aOn) noexcept
{
    if (aOn)
        aSource.RegisterSink(&s_sinks);
    else
        aSource.UnRegisterSink(&s_sinks);
}

void ListenAll(EventDispatcherManager& aManager, bool aOn) noexcept
{
    Listen(aManager.spellCastEvent, aOn);
    Listen(aManager.magicEffectApplyEvent, aOn);
    Listen(aManager.activeEffectApplyRemove, aOn);
    Listen(aManager.questStartStopEvent, aOn);
    Listen(aManager.equipEvent, aOn);
    Listen(aManager.hitEvent, aOn);
    Listen(aManager.eventProcessedEvent, aOn);
    Listen(aManager.activateEvent, aOn);
    Listen(aManager.lockChangedEvent, aOn);
    Listen(aManager.openCloseEvent, aOn);
    Listen(aManager.moveAttachDetachEvent, aOn);
    Listen(aManager.objectLoadedEvent, aOn);
    Listen(aManager.containerChangedEvent, aOn);
}
} // namespace

void EngineEvents::Poll() noexcept
{
    if (m_listening)
        return;
    if (EventDispatcherManager* pManager = EventDispatcherManager::Get())
    {
        ListenAll(*pManager, true);
        m_listening = true;
    }
}

void EngineEvents::Stop() noexcept
{
    if (!m_listening)
        return;
    if (EventDispatcherManager* pManager = EventDispatcherManager::Get())
        ListenAll(*pManager, false);
    m_listening = false;
}

nlohmann::json EngineEvents::Since(uint64_t aSince) const
{
    auto events = nlohmann::json::array();
    std::scoped_lock _{s_lock};
    for (const auto& cEvent : s_events)
        if (cEvent["seq"].get<uint64_t>() >= aSince)
            events.push_back(cEvent);
    return {{"ok", true}, {"events", events}, {"next", s_next}};
}
} // namespace TestHarness
