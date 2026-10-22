#include <PlayerCharacter.h>

#include <Forms/TESTopicInfo.h>
#include <HookSwitch.h>
#include <Games/ActorExtension.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_Master_Behavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>

#include <Games/Overrides.h>

#include <Games/Events.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/AddExperienceEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/RemoveWaypointEvent.h>

#include <World.h>

#include <Games/Skyrim/Forms/ActorValueInfo.h>
#include <Games/ActorExtension.h>
#include <Games/TES.h>
#include <Games/References.h>
#include <Forms/TESWorldSpace.h>

#include <Forms/TESObjectCELL.h>
#include <Interface/UI.h>

#include <ModCompat/BehaviorVar.h>

int32_t PlayerCharacter::LastUsedCombatSkill = -1;

using TPickUpObject = char(PlayerCharacter*, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, bool aUnk2);
using TSetBeastForm = void(void*, void* apUnk1, void* apUnk2, bool aEntering);
using TAddSkillExperience = void(PlayerCharacter*, int32_t aSkill, float aExperience);
using TCalculateExperience = bool(int32_t*, float* aFactor, float* aBonus, float* aUnk1, float* aUnk2);
using TSetWaypoint = void(PlayerCharacter*, NiPoint3* apPosition, TESForm* apWorldOrCell);
using TRemoveWaypoint = void(PlayerCharacter*);

static TPickUpObject* RealPickUpObject = nullptr;
static TSetBeastForm* RealSetBeastForm = nullptr;
static TAddSkillExperience* RealAddSkillExperience = nullptr;
static TCalculateExperience* RealCalculateExperience = nullptr;
static TSetWaypoint* RealSetWaypoint = nullptr;
static TRemoveWaypoint* RealRemoveWaypoint = nullptr;

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

void PlayerCharacter::SetGodMode(bool aSet) noexcept
{
    POINTER_SKYRIMSE(bool, bGodMode, 404238);
    *bGodMode.Get() = aSet;
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

NiPoint3 PlayerCharacter::RespawnPlayer() noexcept
{
    // Make bleedout state recoverable
    SetNoBleedoutRecovery(false);

    DispelAllSpells();

    // Reset health to max
    // TODO(cosideci): there's a cleaner way to do this
    ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 1000000);

    TESObjectCELL* pCell = nullptr;

    if (GetWorldSpace())
    {
        // TP to Whiterun temple when killed in world space
        TES* pTes = TES::Get();
        pCell = ModManager::Get()->GetCellFromCoordinates(pTes->centerGridX, pTes->centerGridY, GetWorldSpace(), false);
    }
    else
    {
        // TP to start of cell when killed in an interior
        pCell = GetParentCell();
    }

    NiPoint3 pos{};
    NiPoint3 rot{};
    pCell->GetCOCPlacementInfo(&pos, &rot, true);

    MoveTo(pCell, pos);

    // Make bleedout state unrecoverable again for when the player goes down the next time
    SetNoBleedoutRecovery(true);

    return pos;
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

char HookPickUpObject(PlayerCharacter* apThis, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, bool aUnk2)
{
    auto& modSystem = World::Get().GetModSystem();

    Inventory::Entry item{};
    modSystem.GetServerModId(apObject->baseForm->formID, item.BaseId);
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

void HookSetBeastForm(void* apThis, void* apUnk1, void* apUnk2, bool aEntering)
{
    if (!aEntering)
    {
        PlayerCharacter::Get()->GetExtension()->GraphDescriptorHash = BehaviorVar::GetHumanoidHash();
        World::Get().GetRunner().Trigger(BeastFormChangeEvent());
    }

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

static TiltedPhoques::Initializer s_playerCharacterHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TPickUpObject, s_pickUpObject, 40533);
        POINTER_SKYRIMSE(TSetBeastForm, s_setBeastForm, 55497);
        POINTER_SKYRIMSE(TAddSkillExperience, s_addSkillExperience, 40488);
        POINTER_SKYRIMSE(TCalculateExperience, s_calculateExperience, 27244);
        POINTER_SKYRIMSE(TSetWaypoint, s_setWaypoint, 40535);
        POINTER_SKYRIMSE(TRemoveWaypoint, s_removeWaypoint, 40536);

        RealPickUpObject = s_pickUpObject.Get();
        RealSetBeastForm = s_setBeastForm.Get();
        RealAddSkillExperience = s_addSkillExperience.Get();
        RealCalculateExperience = s_calculateExperience.Get();
        RealSetWaypoint = s_setWaypoint.Get();
        RealRemoveWaypoint = s_removeWaypoint.Get();

        TP_HOOK(&RealPickUpObject, HookPickUpObject);
        TP_HOOK(&RealSetBeastForm, HookSetBeastForm);
        TP_HOOK(&RealAddSkillExperience, HookAddSkillExperience);
        TP_HOOK(&RealCalculateExperience, HookCalculateExperience);
        TP_HOOK(&RealSetWaypoint, HookSetWaypoint);
        TP_HOOK(&RealRemoveWaypoint, HookRemoveWaypoint);
    });
