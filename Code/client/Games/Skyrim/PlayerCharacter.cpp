#include <PlayerCharacter.h>

#include <Forms/TESTopicInfo.h>
#include <Havok/Ragdoll.h>
#include <HookSwitch.h>
#include <Games/ActorExtension.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_Master_Behavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>

#include <Games/Overrides.h>

#include <Games/Events.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Events/PlayerProgressEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/PlayerTravelQueuedEvent.h>
#include <Papyrus/GuestScripts.h>
#include <Services/TransportService.h>

#include <World.h>

#include <Games/Skyrim/Forms/ActorValueInfo.h>
#include <Games/ActorExtension.h>
#include <Games/TES.h>
#include <Camera/PlayerCamera.h>
#include <Forms/SpellItem.h>
#include <Forms/TESObjectWEAP.h>
#include <AI/AIProcess.h>
#include <DefaultObjectManager.h>
#include <EquipManager.h>
#include <Magic/MagicCaster.h>
#include <Games/References.h>
#include <Forms/TESWorldSpace.h>

#include <Forms/TESObjectCELL.h>
#include <Interface/UI.h>

#include <ModCompat/BehaviorVar.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>
#include <FormIds.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <optional>
#include <utility>

using TPickUpObject = char(PlayerCharacter*, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, bool aUnk2);
using TSetBeastForm = void(void*, void* apUnk1, void* apUnk2, bool aEntering);
using TAddSkillExperience = void(PlayerCharacter*, int32_t aSkill, float aExperience, TESForm* apAdvanceObject, uint32_t aAdvanceAction);
using TSetWaypoint = void(PlayerCharacter*, NiPoint3* apPosition, TESForm* apWorldOrCell);
using TRemoveWaypoint = void(PlayerCharacter*);
using TSetDifficulty = void(PlayerCharacter*, int32_t aDifficulty);

static TPickUpObject* RealPickUpObject = nullptr;
static TSetBeastForm* RealSetBeastForm = nullptr;
static TAddSkillExperience* RealAddSkillExperience = nullptr;
static TSetWaypoint* RealSetWaypoint = nullptr;
static TRemoveWaypoint* RealRemoveWaypoint = nullptr;
static TSetDifficulty* RealSetDifficulty = nullptr;

PlayerCharacter* PlayerCharacter::Get() noexcept
{
    POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069);

    return *s_character.Get();
}

const GameArray<TintMask*>& PlayerCharacter::GetTints() const noexcept
{
    if (overlayTints)
        return *overlayTints;

    return baseTints;
}

void PlayerCharacter::RebuildTintMasks() noexcept
{
    using TRebuildTintMasks = void(PlayerCharacter*);
    POINTER_SKYRIMSE(TRebuildTintMasks, s_rebuildTintMasks, 40696);
    s_rebuildTintMasks(this);
}

void PlayerCharacter::SetDifficulty(const int32_t aDifficulty, bool aForceUpdate, bool aExpectGameDataLoaded) noexcept
{
    if (aDifficulty > 5 || aDifficulty < 0)
        return;

    int32_t* difficultySetting = Settings::GetDifficulty();
    *difficultySetting = difficulty = aDifficulty;
}

bool PlayerCharacter::SaidRecently(const TESTopicInfo* apInfo) const noexcept
{
    using TSaidRecently = bool(const PlayerCharacter*, const TESTopicInfo* apInfo);
    POINTER_SKYRIMSE(TSaidRecently, s_saidRecently, 40694);
    return apInfo && apInfo->timeUntilReset && (s_saidRecently.Get())(this, apInfo);
}

void PlayerCharacter::RememberSaid(const TESTopicInfo* apInfo) noexcept
{
    using TRememberSaid = void(PlayerCharacter*, const TESTopicInfo* apInfo);
    POINTER_SKYRIMSE(TRememberSaid, s_rememberSaid, 40692);
    if (apInfo && apInfo->timeUntilReset)
        (s_rememberSaid.Get())(this, apInfo);
}

void PlayerCharacter::SetAIDriven(bool aDriven) noexcept
{
    using TSetAIDriven = void(PlayerCharacter*, bool);
    POINTER_SKYRIMSE(TSetAIDriven, s_setAiDriven, 40586);
    s_setAiDriven.Get()(this, aDriven);
}

void PlayerCharacter::Revive() noexcept
{
    // What its hands hold: still marked worn after Resurrect, but no longer in hand.
    TESForm* hands[2]{};
    if (currentProcess)
        std::copy_n(currentProcess->equippedObject, 2, hands);
    // Timed effects stay gone; abilities (base, race, perks, AddSpell) and enchantments come back.
    Resurrect(false);
    CastPermanentMagic(false, true, true, true);
    CastPermanentMagic(true, false, false, false);
    ApplyPerksFromBase();
    if (MagicCaster* pCaster = GetMagicCaster(MagicSystem::INSTANT))
    {
        // MagicCaster's permanent cast (34402), as CastPermanentMagic casts the base's.
        using TCastPermanent = void(MagicCaster*, SpellItem*, bool);
        POINTER_SKYRIMSE(TCastPermanent, s_castPermanent, 34402);
        for (uint32_t i = 0; i < addedSpells.size; ++i)
            if (SpellItem* pSpell = addedSpells.Get(i))
                s_castPermanent(pCaster, pSpell, true);
    }
    DoReset3D(true);
    // A death ending a kill move leaves the ragdoll simulating through the reset: out of the world, as a living player's is.
    RemoveRagdollFromWorld(*this);
    // Its graph runs and it speaks again: death stopped both, and only a load sets them for the player.
    flags2 |= ActorFlags::SHOULD_ANIM_GRAPH_UPDATE | ActorFlags::CAN_SPEAK;
    // Back in its hands.
    for (uint32_t hand = 0; hand < 2; ++hand)
    {
        TESForm* pItem = hands[hand];
        // A two-handed weapon is in both: equipped once.
        if (!pItem || (hand == 0 && pItem == hands[1]) || (currentProcess && currentProcess->equippedObject[hand] == pItem))
            continue;
        if (Cast<SpellItem>(pItem))
            EquipManager::Get()->EquipSpell(this, pItem, hand);
        else
        {
            // Still marked worn: taken off first, or the inventory keeps a stale worn entry.
            const auto& cObjects = DefaultObjectManager::Get();
            TESForm* pSlot = Cast<TESObjectWEAP>(pItem) ? (hand ? cObjects.rightEquipSlot : cObjects.leftEquipSlot) : nullptr;
            EquipManager::Get()->UnEquip(this, pItem, nullptr, 1, pSlot, false, false, false, false, nullptr);
            EquipManager::Get()->Equip(this, pItem, nullptr, 1, pSlot, false, false, false, false);
        }
    }
    if (PlayerCamera* pCamera = PlayerCamera::Get())
        pCamera->LeaveBleedout(false);
    // Its death action is over: a copy made now starts standing.
    GetExtension()->LatestAnimation = {};
}

void PlayerCharacter::HoldDeathReload(bool aHold) noexcept
{
    POINTER_SKYRIMSE(Setting, s_reloadTime, 371051);
    static std::optional<uint64_t> s_saved;
    if (aHold == s_saved.has_value())
        return;
    if (aHold)
    {
        s_saved = s_reloadTime->data;
        s_reloadTime->data = std::bit_cast<uint32_t>(1e9f);
    }
    else
        s_reloadTime->data = *std::exchange(s_saved, std::nullopt);
}

void PlayerCharacter::PayCrimeGoldToAllFactions() noexcept
{
    // Yes, yes, this isn't great, but there's no "pay fines everywhere" function
    const uint32_t crimeFactionIds[]{0x28170, 0x267E3, 0x29DB0, 0x2816D, 0x2816e, 0x2816C, 0x2816B, 0x267EA, 0x2816F, 0x4018279};

    for (uint32_t crimeFactionId : crimeFactionIds)
    {
        TESFaction* pCrimeFaction = Cast<TESFaction>(TESForm::GetById(crimeFactionId));
        if (!pCrimeFaction)
        {
            spdlog::error("This isn't a crime faction! {:X}", crimeFactionId);
            continue;
        }

        PayFine(pCrimeFaction, false, false);
    }
}

void PlayerCharacter::SetWaypoint(NiPoint3* apPosition, TESForm* apWorldOrCell) noexcept
{
    return RealSetWaypoint(this, apPosition, apWorldOrCell);
}

void PlayerCharacter::RemoveWaypoint() noexcept
{
    return RealRemoveWaypoint(this);
}

static std::function<bool(TESObjectREFR&)> s_pickUpGate;
static thread_local bool t_pickUpGranted = false;

char HookPickUpObject(PlayerCharacter* apThis, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, bool aUnk2)
{
    // Taken already (the host granted it to another player this frame): the engine would hand it out again.
    if (apObject && apObject->IsDeleted())
        return false;
    // A guest's reach for the party's loose item: the host's game decides who gets it.
    if (!t_pickUpGranted && s_pickUpGate && apObject && s_pickUpGate(*apObject))
    {
        World::Get().GetRunner().Trigger(ItemPickUpEvent{apObject->formID});
        return false;
    }

    Inventory::Entry item{};
    item.BaseId = ToSession(apObject->baseForm->formID).value_or(GameId{});
    item.Count = aCount;

    if (apObject->GetExtraDataList() && !ScopedExtraDataOverride::IsOverriden())
    {
        ScopedExtraDataOverride _;
        apThis->GetItemFromExtraData(item, apObject->GetExtraDataList());
    }

    // This is here so that objects that are picked up on both clients, aka non temps, are synced through activation sync.
    // The inventory change event should always be sent to the server, otherwise the server inventory won't be updated.
    bool shouldUpdateClients = apObject->IsTemporary() && !ScopedActivateOverride::IsOverriden();

    World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item), false, shouldUpdateClients));

    ScopedInventoryOverride _;

    return RealPickUpObject(apThis, apObject, aCount, aUnk1, aUnk2);
}

void PlayerCharacter::SetPickUpGate(std::function<bool(TESObjectREFR&)> aGate) noexcept
{
    s_pickUpGate = std::move(aGate);
}

void PlayerCharacter::PickUpGranted(TESObjectREFR& aObject, int32_t aCount) noexcept
{
    t_pickUpGranted = true;
    HookPickUpObject(this, &aObject, aCount, false, true);
    t_pickUpGranted = false;
}

void HookSetBeastForm(void* apThis, void* apUnk1, void* apUnk2, bool aEntering)
{
    // The race switched back before (ShiftBack's SetRace): that respawned the copies (PlayerLooksChangeEvent).
    if (!aEntering)
        PlayerCharacter::Get()->GetExtension()->GraphDescriptorHash = BehaviorVar::GetHumanoidHash();

    RealSetBeastForm(apThis, apUnk1, apUnk2, aEntering);
}

// PlayerCharacter::AddSkillExperience (40488, CommonLibSSE-NG): the advance object is r9, action the fifth argument.
void HookAddSkillExperience(PlayerCharacter* apThis, int32_t aSkill, float aExperience, TESForm* apAdvanceObject, uint32_t aAdvanceAction)
{
    RealAddSkillExperience(apThis, aSkill, aExperience, apAdvanceObject, aAdvanceAction);
    if (aExperience > 0.f && aSkill >= ActorValueInfo::kOneHanded && aSkill <= ActorValueInfo::kEnchanting)
        World::Get().GetRunner().Trigger(PlayerProgressEvent{});
}

// The map menu places the player's marker (MapMenu::PlaceMarker 53113, then the terrain pick 53114).
void HookSetWaypoint(PlayerCharacter* apThis, NiPoint3* apPosition, TESForm* apWorldOrCell)
{
    Vector3_NetQuantize position{};
    position.x = apPosition->x;
    position.y = apPosition->y;
    position.z = apPosition->z;

    World::Get().GetRunner().Trigger(SetWaypointEvent(position, apWorldOrCell ? apWorldOrCell->formID : 0));

    return RealSetWaypoint(apThis, apPosition, apWorldOrCell);
}

// Only the map's "remove marker" is the player's choice; the engine also clears it outside the map.
void HookRemoveWaypoint(PlayerCharacter* apThis)
{
    static const BSFixedString s_mapMenu("MapMenu");
    if (UI* pUI = UI::Get(); pUI && pUI->GetMenuOpen(s_mapMenu))
        World::Get().GetRunner().Trigger(RemoveWaypointEvent());

    return RealRemoveWaypoint(apThis);
}

// Every difficulty the engine sets, after writing iDifficulty: the settings menu (53310), its defaults (36622), the
// saved settings applied (36598) and a profile's.
void HookSetDifficulty(PlayerCharacter* apThis, int32_t aDifficulty)
{
    RealSetDifficulty(apThis, aDifficulty);
    World::Get().GetRunner().Trigger(DifficultyChangeEvent{static_cast<uint32_t>(*Settings::GetDifficulty())});
}

namespace
{
// The player's PositionPlayerEvent (CommonLibSSE; skymp's "positionPlayer"), sent by 40438 for every
// travel: doors, fast travel, MoveTo, coc (docs/re/systems/player-travel.md).
struct PositionPlayerEvent
{
    enum Type : uint32_t
    {
        kPre = 0,
        kPreUpdatePackages = 1,
        kPostUpdatePackages = 2,
        kPost = 3,
        kFinish = 4
    };
    Type Phase;
};

struct PositionPlayerSink final : BSTEventSink<PositionPlayerEvent>
{
    BSTEventResult OnEvent(const PositionPlayerEvent* apEvent, const EventDispatcher<PositionPlayerEvent>*) override
    {
        spdlog::info("PositionPlayerEvent phase {}", static_cast<uint32_t>(apEvent->Phase));
        if (apEvent->Phase == PositionPlayerEvent::kFinish)
            Teleported = true;
        return BSTEventResult::kOk;
    }

    bool Teleported = false;
};

PositionPlayerSink s_positionPlayerSink;
bool s_positionPlayerRegistered = false;
} // namespace

namespace
{
// PlayerCharacter's BSTEventSource<PositionPlayerEvent> base: +0x388 on 1.6.1170, from the game's
// RTTI (its base class descriptor's mdisp); CommonLibSSE's 0x380 is the SE layout.
EventDispatcher<PositionPlayerEvent>* PositionPlayerSource() noexcept
{
    constexpr size_t cPositionPlayerSource = 0x388;
    auto* pPlayer = PlayerCharacter::Get();
    return pPlayer ? reinterpret_cast<EventDispatcher<PositionPlayerEvent>*>(reinterpret_cast<uint8_t*>(pPlayer) + cPositionPlayerSource)
                   : nullptr;
}
} // namespace

bool PlayerCharacter::ConsumeTeleported() noexcept
{
    if (!s_positionPlayerRegistered)
    {
        auto* pSource = PositionPlayerSource();
        if (!pSource)
            return false;
        pSource->RegisterSink(&s_positionPlayerSink);
        s_positionPlayerRegistered = true;
    }
    return std::exchange(s_positionPlayerSink.Teleported, false);
}

void PlayerCharacter::StopTeleportEvents() noexcept
{
    // A hot reload leaves this DLL's code loaded but its state torn down: a sink left registered
    // would be called into it on the next teleport.
    if (!std::exchange(s_positionPlayerRegistered, false))
        return;
    if (auto* pSource = PositionPlayerSource())
        pSource->UnRegisterSink(&s_positionPlayerSink);
}

using TProcessQueuedTravel = uintptr_t(PlayerCharacter*);
static TProcessQueuedTravel* RealProcessQueuedTravel;
static uint64_t s_queuedTravel = 0;
static std::atomic<uint64_t> s_announcedTravel = 0;
static std::optional<PlayerTargetLocation> s_pendingTravel;

static uintptr_t HookProcessQueuedTravel(PlayerCharacter* apPlayer)
{
    const auto& cTarget = apPlayer->queuedTarget;
    if (!cTarget.Valid || !World::Get().GetTransport().IsOnline())
    {
        s_pendingTravel.reset();
        return RealProcessQueuedTravel(apPlayer);
    }
    // Another game's party call moved this player (a story's MoveTo on each player): its own move, it leads no one.
    if (GuestScripts::TakeMirroredMove())
    {
        s_pendingTravel.reset();
        return RealProcessQueuedTravel(apPlayer);
    }
    // coc writes queuedTarget inline, bypassing 40442. Observe it where every move is consumed (40438).
    if (!s_pendingTravel || s_pendingTravel->World != cTarget.World || s_pendingTravel->Interior != cTarget.Interior ||
        s_pendingTravel->Position.x != cTarget.Position.x || s_pendingTravel->Position.y != cTarget.Position.y ||
        s_pendingTravel->Position.z != cTarget.Position.z || s_pendingTravel->Angle.z != cTarget.Angle.z)
    {
        s_pendingTravel = cTarget;
        const uint64_t cQueued = ++s_queuedTravel;
        const auto& cAt = cTarget.Position;
        const PlayerTravelQueuedEvent cEvent{
            cTarget.World ? cTarget.World->formID : 0,
            cTarget.Interior ? cTarget.Interior->formID : 0,
            {cAt.x, cAt.y, cAt.z}, cTarget.Angle.z};
        World::Get().GetRunner().Queue([cEvent, cQueued] {
            World::Get().GetDispatcher().trigger(cEvent);
            s_announcedTravel = cQueued;
        });
    }
    // Send the destination before the native load blocks this game's updates.
    if (s_announcedTravel.load() != s_queuedTravel)
        return 0;
    s_pendingTravel.reset();
    return RealProcessQueuedTravel(apPlayer);
}

static Hooks::Initializer s_playerCharacterHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(40438, &RealProcessQueuedTravel, HookProcessQueuedTravel);
        HOOK_ID(40533, &RealPickUpObject, HookPickUpObject);
        HOOK_ID(55497, &RealSetBeastForm, HookSetBeastForm);
        HOOK_ID(40488, &RealAddSkillExperience, HookAddSkillExperience);
        HOOK_ID(40535, &RealSetWaypoint, HookSetWaypoint);
        HOOK_ID(40536, &RealRemoveWaypoint, HookRemoveWaypoint);
        HOOK_ID(40563, &RealSetDifficulty, HookSetDifficulty);
    });
