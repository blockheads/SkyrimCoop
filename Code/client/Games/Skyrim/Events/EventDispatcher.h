#pragma once

#include <TESObjectREFR.h>

template <class T> struct BSTEventSink;

// Very nasty work around to avoid template code duplication
namespace details
{
void InternalRegisterSink(void* apEventDispatcher, void* apSink) noexcept;
void InternalUnRegisterSink(void* apEventDispatcher, void* apSink) noexcept;
void InternalPushEvent(void* apEventDispatcher, void* apEvent) noexcept;
} // namespace details

template <class T> struct EventDispatcher
{
    void RegisterSink(BSTEventSink<T>* apSink) noexcept { details::InternalRegisterSink(reinterpret_cast<void*>(this), reinterpret_cast<void*>(apSink)); }

    void UnRegisterSink(BSTEventSink<T>* apSink) noexcept { details::InternalUnRegisterSink(reinterpret_cast<void*>(this), reinterpret_cast<void*>(apSink)); }

    void PushEvent(const T* apEvent) noexcept { details::InternalPushEvent(reinterpret_cast<void*>(this), reinterpret_cast<void*>(apEvent)); }

    uint8_t pad0[0x58];
};

struct UnknownEvent
{
};

// The story manager finished a story event (BGSStoryTeller::Update 32483): its id (AddEvent_Impl's) and
// whether it started a quest.
struct BGSEventProcessedEvent
{
    uint32_t eventId;  // 00
    bool startedQuest; // 04
};
static_assert(offsetof(BGSEventProcessedEvent, startedQuest) == 0x4);

struct TESActivateEvent
{
    TESObjectREFR* object;
};

// The event behind Papyrus OnEffectStart / OnEffectFinish (CommonLibSSE-NG TESActiveEffectApplyRemoveEvent).
struct TESActiveEffectApplyRemove
{
    TESObjectREFR* hCaster;          // 00
    TESObjectREFR* hTarget;          // 08
    uint16_t usActiveEffectUniqueID; // 10
    bool bIsApplied;                 // 12
};
static_assert(offsetof(TESActiveEffectApplyRemove, usActiveEffectUniqueID) == 0x10 && offsetof(TESActiveEffectApplyRemove, bIsApplied) == 0x12);

struct TESActorLocationChangeEvent
{
};

struct TESBookReadEvent
{
};

struct TESCellAttachDetachEvent
{
};

struct TESCellFullyLoadedEvent
{
};

struct TESCombatEvent
{
};

// Sent by InventoryChanges::SendContainerChangedEvent (16149) on every add and remove but a leveled roll.
struct TESContainerChangedEvent
{
    uint32_t oldContainerID; // 00, 0 when the item came from nowhere
    uint32_t newContainerID; // 04, 0 when it went nowhere
    uint32_t baseObjectID;   // 08
    int32_t itemCount;       // 0C
    uint32_t referenceHandle; // 10
    uint16_t uniqueID;       // 14
    uint16_t pad16;
};

struct TESDeathEvent
{
    TESObjectREFR* pActorDying;
    TESObjectREFR* pActorKiller;
    bool isDead;
};

struct TESDestructionStageChangedEvent
{
};

struct TESEnterBleedoutEvent
{
};

// CommonLibSSE-NG TESEquipEvent.
struct TESEquipEvent
{
    TESObjectREFR* actor;  // 00
    uint32_t baseObject;   // 08
    uint32_t originalRefr; // 0C
    uint16_t uniqueId;     // 10
    bool equipped;         // 12
};
static_assert(offsetof(TESEquipEvent, baseObject) == 0x8 && offsetof(TESEquipEvent, equipped) == 0x12);

struct TESFormDeleteEvent
{
};

struct TESFurnitureEvent
{
};

struct TESGrabReleaseEvent
{
};

// CommonLibSSE-NG TESHitEvent.
struct TESHitEvent
{
    TESObjectREFR* hit;    // 00 target
    TESObjectREFR* hitter; // 08 cause
    uint32_t source;       // 10 weapon or spell
    uint32_t projectile;   // 14
    uint8_t flags;         // 18
};
static_assert(offsetof(TESHitEvent, projectile) == 0x14);

struct TESLoadGameEvent
{
};

struct TESLockChangedEvent
{
};

// CommonLibSSE-NG TESMagicEffectApplyEvent.
struct TESMagicEffectApplyEvent
{
    TESObjectREFR* hTarget;       // 00
    TESObjectREFR* hCaster;       // 08
    uint32_t uiMagicEffectFormID; // 10
};
static_assert(offsetof(TESMagicEffectApplyEvent, uiMagicEffectFormID) == 0x10);

struct TESMagicWardHitEvent
{
};

struct TESMoveAttachDetachEvent
{
};

struct TESObjectLoadedEvent
{
};

struct TESObjectREFRTranslationEvent
{
};

struct TESOpenCloseEvent
{
};

struct TESPackageEvent
{
};

struct TESInitScriptEvent
{
};

struct TESPerkEntryRunEvent
{
};

struct TESQuestInitEvent
{
    uint32_t formId;
};

struct TESQuestStageEvent
{
    void* callback;
    uint32_t formId;
    uint16_t stageId;
    bool bUnk;
};

// CommonLibSSE-NG TESQuestStartStopEvent.
struct TESQuestStartStopEvent
{
    uint32_t formId; // 00
    bool started;    // 04
    bool failed;     // 05
};
static_assert(offsetof(TESQuestStartStopEvent, started) == 0x4);

struct TESQuestStageItemDoneEvent
{
    uint32_t formId;
    uint16_t stageId;
    bool unk;
};

struct TESResetEvent
{
};

struct TESResolveNPCTemplatesEvent
{
};

struct TESSceneEvent
{
};

struct TESSceneActionEvent
{
};

struct TESScenePhaseEvent
{
};

struct TESSellEvent
{
};

struct TESSleepStartEvent
{
};

struct TESSleepStopEvent
{
};

// CommonLibSSE-NG TESSpellCastEvent.
struct TESSpellCastEvent
{
    TESObjectREFR* object; // 00
    uint32_t spell;        // 08
};
static_assert(offsetof(TESSpellCastEvent, spell) == 0x8);

struct TESPlayerBowShotEvent
{
};

struct TESTopicInfoEvent
{
    TESObjectREFR* hSpeakerRef;
    void* pCallback;
    uint32_t uiTopicInfoFormID;
    int32_t eType;
    uint16_t usStage;
};

struct TESTrackedStatsEvent
{
};

struct TESTrapHitEvent
{
};

struct TESTriggerEvent
{
    TESObjectREFR* pTrigger;
    TESObjectREFR* pActionRef;
};

struct TESTriggerEnterEvent
{
    TESObjectREFR* pTrigger;
    TESObjectREFR* pActionRef;
};

struct TESTriggerLeaveEvent
{
    TESObjectREFR* pTrigger;
    TESObjectREFR* pActionRef;
};

struct TESUniqueIDChangeEvent
{
};

struct TESSwitchRaceCompleteEvent
{
};

struct TESFastTravelEndEvent
{
};

struct EventDispatcherManager
{
    static EventDispatcherManager* Get() noexcept;

    EventDispatcher<BGSEventProcessedEvent> eventProcessedEvent;
    EventDispatcher<TESActivateEvent> activateEvent;
    EventDispatcher<TESActiveEffectApplyRemove> activeEffectApplyRemove;
    EventDispatcher<TESActorLocationChangeEvent> actorLocationChangeEvent;
    EventDispatcher<TESBookReadEvent> bookReadEvent;
    EventDispatcher<TESCellAttachDetachEvent> cellAttachDetachEvent;
    EventDispatcher<TESCellFullyLoadedEvent> cellFullyLoadedEvent;
    EventDispatcher<UnknownEvent> unknownDispatcher8; // apply decals event
    EventDispatcher<TESCombatEvent> combatEvent;
    EventDispatcher<TESContainerChangedEvent> containerChangedEvent;
    EventDispatcher<TESDeathEvent> deathEvent;
    EventDispatcher<TESDestructionStageChangedEvent> destructionStageChangedEventDispatcher;
    EventDispatcher<TESEnterBleedoutEvent> enterBleedoutEvent;
    EventDispatcher<TESEquipEvent> equipEvent;
    EventDispatcher<TESFormDeleteEvent> formDeleteEvent;
    EventDispatcher<TESFurnitureEvent> furnitureEvent;
    EventDispatcher<TESGrabReleaseEvent> grabReleaseEvent;
    EventDispatcher<TESHitEvent> hitEvent; // validated
    EventDispatcher<TESInitScriptEvent> initScriptEvent;
    EventDispatcher<TESLoadGameEvent> loadGameEvent;
    EventDispatcher<TESLockChangedEvent> lockChangedEvent;
    EventDispatcher<TESMagicEffectApplyEvent> magicEffectApplyEvent;
    EventDispatcher<TESMagicWardHitEvent> magicWardHitEvent;
    EventDispatcher<TESMoveAttachDetachEvent> moveAttachDetachEvent;
    EventDispatcher<TESObjectLoadedEvent> objectLoadedEvent;
    EventDispatcher<TESObjectREFRTranslationEvent> objectREFRTranslationEvent;
    EventDispatcher<TESOpenCloseEvent> openCloseEvent;
    EventDispatcher<TESPackageEvent> packageEvent;
    EventDispatcher<TESPerkEntryRunEvent> perkEntryRunEvent;
    EventDispatcher<TESQuestInitEvent> questInitEvent; // 9F8
    EventDispatcher<TESQuestStageEvent> questStageEvent;
    EventDispatcher<TESQuestStageItemDoneEvent> questStageItemDoneEvent; // AA8, validated StageItemFinishedCallback::TriggerItemDoneEvent
    EventDispatcher<TESQuestStartStopEvent> questStartStopEvent;         // TESResolveNPCTemplatesEvent
    EventDispatcher<TESResetEvent> resetEvent;                           // validated 0xB58
    EventDispatcher<TESResolveNPCTemplatesEvent> resolveNPCTemplatesEvent;
    EventDispatcher<TESSceneEvent> sceneEvent;
    EventDispatcher<TESSceneActionEvent> sceneActionEvent;
    EventDispatcher<TESScenePhaseEvent> scenePhaseEvent;
    EventDispatcher<TESSellEvent> sellEvent;
    EventDispatcher<TESSleepStartEvent> unknownDispatcher39;
    EventDispatcher<TESSleepStopEvent> sleepStopEvent;
    EventDispatcher<TESSpellCastEvent> spellCastEvent;
    EventDispatcher<TESPlayerBowShotEvent> unknownDispatcher42;
    EventDispatcher<TESTopicInfoEvent> topicInfoEvent;
    EventDispatcher<TESTrackedStatsEvent> trackedStatsEvent;
    EventDispatcher<TESTrapHitEvent> trapHitEvent;
    EventDispatcher<TESTriggerEvent> triggerEvent;
    EventDispatcher<TESTriggerEnterEvent> triggerEnterEvent;
    EventDispatcher<TESTriggerLeaveEvent> triggerLeaveEvent;
    EventDispatcher<TESUniqueIDChangeEvent> uniqueIDChangeEvent;
    EventDispatcher<UnknownEvent> unknownDispatcher50; // waitevent
    EventDispatcher<UnknownEvent> unknownDispatcher51; // TESWaitStopEvent
    EventDispatcher<TESSwitchRaceCompleteEvent> switchRaceCompleteEvent;
    EventDispatcher<TESFastTravelEndEvent> fastTravelEndEvent;
};

// constexpr auto x = offsetof(EventDispatcherManager, unkx);

SKYRIM_STRUCT_ASSERT(sizeof(EventDispatcherManager) == 4752);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, activateEvent) == 88);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, deathEvent) == 880);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, lockChangedEvent) == 1760);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, furnitureEvent) == 1320);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, containerChangedEvent) == 792); // 16149 sends to +0x318
SKYRIM_STRUCT_ASSERT(sizeof(TESContainerChangedEvent) == 0x18);
SKYRIM_STRUCT_ASSERT(offsetof(TESContainerChangedEvent, itemCount) == 0x0C);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, resetEvent) == 2904);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, trackedStatsEvent) == 3872);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, triggerEvent) == 4048);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, switchRaceCompleteEvent) == 4576);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, fastTravelEndEvent) == 4664);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, questInitEvent) == 0x9F8);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, questStageEvent) == 0xA50);
SKYRIM_STRUCT_ASSERT(offsetof(EventDispatcherManager, questStartStopEvent) == 0xB00);
