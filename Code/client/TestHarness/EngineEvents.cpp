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
                     BSTEventSink<BGSEventProcessedEvent>
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
    Listen(aManager.eventProcessedEvent, aOn);
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
