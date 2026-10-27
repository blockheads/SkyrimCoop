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
#include <Events/AddExperienceEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/DifficultyChangeEvent.h>

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
#include <bit>
#include <optional>
#include <utility>

int32_t PlayerCharacter::LastUsedCombatSkill = -1;

using TPickUpObject = char(PlayerCharacter*, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, bool aUnk2);
using TSetBeastForm = void(void*, void* apUnk1, void* apUnk2, bool aEntering);
using TAddSkillExperience = void(PlayerCharacter*, int32_t aSkill, float aExperience);
using TCalculateExperience = bool(int32_t*, float* aFactor, float* aBonus, float* aUnk1, float* aUnk2);
using TSetWaypoint = void(PlayerCharacter*, NiPoint3* apPosition, TESForm* apWorldOrCell);
using TRemoveWaypoint = void(PlayerCharacter*);
using TSetDifficulty = void(PlayerCharacter*, int32_t aDifficulty);

static TPickUpObject* RealPickUpObject = nullptr;
static TSetBeastForm* RealSetBeastForm = nullptr;
static TAddSkillExperience* RealAddSkillExperience = nullptr;
static TCalculateExperience* RealCalculateExperience = nullptr;
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

void PlayerCharacter::AddSkillExperience(int32_t aSkill, float aExperience) noexcept
{
    Skills::Skill skill = Skills::GetSkillFromActorValue(aSkill);
    float oldExperience = GetSkillExperience(skill);

    ScopedExperienceOverride _;

    RealAddSkillExperience(this, aSkill, aExperience);

    float newExperience = GetSkillExperience(skill);
    float deltaExperience = newExperience - oldExperience;

    spdlog::debug("Added {} experience to skill {}", deltaExperience, aSkill);
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

void HookAddSkillExperience(PlayerCharacter* apThis, int32_t aSkill, float aExperience)
{
    // TODO: armor skills? sneak?
    static const std::unordered_set<int32_t> combatSkills{ActorValueInfo::kAlteration, ActorValueInfo::kConjuration, ActorValueInfo::kDestruction, ActorValueInfo::kIllusion, ActorValueInfo::kRestoration, ActorValueInfo::kOneHanded, ActorValueInfo::kTwoHanded, ActorValueInfo::kMarksman, ActorValueInfo::kBlock};

    Skills::Skill skill = Skills::GetSkillFromActorValue(aSkill);
    float oldExperience = apThis->GetSkillExperience(skill);

    RealAddSkillExperience(apThis, aSkill, aExperience);

    float newExperience = apThis->GetSkillExperience(skill);
    float deltaExperience = newExperience - oldExperience;

    spdlog::debug("Skill (AVI): {}, experience: {}", aSkill, deltaExperience);

    if (combatSkills.contains(aSkill))
    {
        spdlog::debug("Set new last used combat skill to {}.", aSkill);
        PlayerCharacter::LastUsedCombatSkill = aSkill;

        World::Get().GetRunner().Trigger(AddExperienceEvent(deltaExperience));
    }
}

bool HookCalculateExperience(int32_t* apThis, float* aFactor, float* aBonus, float* aUnk1, float* aUnk2)
{
    bool result = RealCalculateExperience(apThis, aFactor, aBonus, aUnk1, aUnk2);

    if (ScopedExperienceOverride::IsOverriden())
    {
        *aFactor = 1.f;
        *aBonus = 0.f;
    }

    return result;
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

static Hooks::Initializer s_playerCharacterHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(40533, &RealPickUpObject, HookPickUpObject);
        HOOK_ID(55497, &RealSetBeastForm, HookSetBeastForm);
        HOOK_ID(40488, &RealAddSkillExperience, HookAddSkillExperience);
        HOOK_ID(27244, &RealCalculateExperience, HookCalculateExperience);
        HOOK_ID(40535, &RealSetWaypoint, HookSetWaypoint);
        HOOK_ID(40536, &RealRemoveWaypoint, HookRemoveWaypoint);
        HOOK_ID(40563, &RealSetDifficulty, HookSetDifficulty);
    });
