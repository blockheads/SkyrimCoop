#include <CrashHandler.h>
#include <HookSwitch.h>
#include <BSCore/LockOrder.h>
#include <Games/EngineNames.h>
#include <Games/References.h>
#include <Games/Skyrim/EquipManager.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <Misc/GameVM.h>
#include <DefaultObjectManager.h>
#include <Forms/TESNPC.h>
#include <Forms/TESFaction.h>
#include <Components/TESActorBaseData.h>
#include <ExtraData/ExtraFactionChanges.h>
#include <Games/Memory.h>
#include <Games/Skyrim/RefrInteraction.h>
#include <Forms/TESLevItem.h>
#include <Combat/CombatController.h>

#include <Events/HealthChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/MountEvent.h>
#include <Events/DismountEvent.h>
#include <Events/DialogueEvent.h>
#include <Events/HitEvent.h>
#include <Events/RemoveSpellEvent.h>

#include <Games/TES.h>
#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Services/PapyrusService.h>

#include <Forms/ActorValueInfo.h>
#include <Forms/TESRace.h>

#include <Effects/ValueModifierEffect.h>

#include <Games/Skyrim/Misc/InventoryEntry.h>
#include <Games/Skyrim/ExtraData/ExtraCount.h>
#include <Games/Misc/ActorKnowledge.h>

#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <Forms/TESObjectCELL.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_BHR_Master.h>

#include <Games/Overrides.h>
#include <Games/Skyrim/BSAnimationGraphManager.h>
#include <Havok/hkbStateMachine.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Forms/BGSOutfit.h>
#include <Forms/TESObjectARMO.h>

#include <ModCompat/BehaviorVar.h>

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <Utils.h>
#include <VersionDb.h>
#include <FormIds.h>

using TRemoveSpell = bool(Actor*, MagicItem*);
using TCharacterConstructor = Actor*(Actor*);
using TCharacterConstructor2 = Actor*(Actor*, uint8_t aUnk);
using TCharacterDestructor = Actor*(Actor*);
using TAddInventoryItem = void(Actor*, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner);
using TPickUpObject = void*(Actor*, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2);
using TDropObject = void*(Actor*, void* apResult, TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation);
using TSetPosition = char(Actor*, NiPoint3& acPosition);
using TApplyMovement = void(Actor*, float aDelta);

using TGetLocation = TESForm*(TESForm*);
static TGetLocation* FUNC_GetActorLocation;

TCharacterConstructor* RealCharacterConstructor;
TCharacterConstructor2* RealCharacterConstructor2;
TCharacterDestructor* RealCharacterDestructor;

static TRemoveSpell* RealRemoveSpell = nullptr;
static TAddInventoryItem* RealAddInventoryItem = nullptr;
static TPickUpObject* RealPickUpObject = nullptr;
static TDropObject* RealDropObject = nullptr;
static TSetPosition* RealSetPosition = nullptr;
static TApplyMovement* RealApplyMovement = nullptr;

float Actor::GetSpeed() noexcept
{
    static BSFixedString speedSampledStr("SpeedSampled");
    float speed = 0.f;
    EngineVirtual(animationGraphHolder).GetVariableFloat(&speedSampledStr, &speed);

    return speed;
}

void Actor::SetSpeed(float aSpeed) noexcept
{
    static BSFixedString speedSampledStr("SpeedSampled");
    EngineVirtual(animationGraphHolder).SetVariableFloat(&speedSampledStr, aSpeed);
}

uint16_t Actor::GetLevel() const noexcept
{
    using TGetLevel = uint16_t(const Actor*);
    POINTER_SKYRIMSE(TGetLevel, s_getLevel, 37334);
    return s_getLevel(this);
}

void Actor::ForcePosition(const NiPoint3& acPosition) noexcept
{
    ScopedReferencesOverride recursionGuard;

    // It just works TM
    SetPosition(acPosition, true);
}

void Actor::QueueUpdate() noexcept
{
    auto* pSetting = INISettingCollection::Get()->GetSetting("bUseFaceGenPreprocessedHeads:General");
    const auto originalValue = pSetting->data;
    pSetting->data = 0;

    using TQueueUpdate = void(Actor*, bool);
    POINTER_SKYRIMSE(TQueueUpdate, QueueUpdate, 40255);

    QueueUpdate(this, true);

    pSetting->data = originalValue;
}

GamePtr<Actor> Actor::Create(TESNPC* apBaseForm) noexcept
{
    auto pActor = New();
    // Prevent saving
    pActor->SetSkipSaveFlag(true);
    pActor->GetExtension()->SetCopy(true);

    const auto pPlayer = static_cast<Actor*>(GetById(0x14));
    auto pCell = pPlayer->parentCell;
    const auto pWorldSpace = pPlayer->GetWorldSpace();

    pActor->SetLevelMod(4);
    pActor->MarkChanged(0x40000000);
    pActor->SetParentCell(pCell);
    pActor->SetBaseForm(apBaseForm);

    auto position = pPlayer->position;
    auto rotation = pPlayer->rotation;

    if (pCell && !(pCell->cellFlags[0] & 1))
        pCell = nullptr;

    ModManager::Get()->Spawn(position, rotation, pCell, pWorldSpace, pActor);

    pActor->ForcePosition(position);

    pActor->GetMagicCaster(MagicSystem::CastingSource::LEFT_HAND);
    pActor->GetMagicCaster(MagicSystem::CastingSource::RIGHT_HAND);
    pActor->GetMagicCaster(MagicSystem::CastingSource::OTHER);

    pActor->flags &= 0xFFDFFFFF;

    return pActor;
}

GamePtr<Actor> Actor::Spawn(uint32_t aBaseFormId) noexcept
{
    TESNPC* pNpc = Cast<TESNPC>(TESForm::GetById(aBaseFormId));
    return Actor::Create(pNpc);
}

void Actor::SetLevelMod(uint32_t aLevel) noexcept
{
    using TActorSetLevelMod = void(ExtraDataList*, uint32_t);
    POINTER_SKYRIMSE(TActorSetLevelMod, realSetLevelMod, 11806);

    const auto pExtraDataList = &extraData;

    realSetLevelMod(pExtraDataList, aLevel);
}

ActorExtension* Actor::GetExtension() noexcept
{
    ActorExtension* pExtension = nullptr;
    if (AsExActor())
        pExtension = static_cast<ActorExtension*>(AsExActor());
    else if (AsExPlayerCharacter())
        pExtension = static_cast<ActorExtension*>(AsExPlayerCharacter());

    if (pExtension)
        pExtension->EnsureCurrent();
    return pExtension;
}

ExActor* Actor::AsExActor() noexcept
{
    if (formType == Type && this != PlayerCharacter::Get())
        return static_cast<ExActor*>(this);

    return nullptr;
}

ExPlayerCharacter* Actor::AsExPlayerCharacter() noexcept
{
    if (this == PlayerCharacter::Get())
        return static_cast<ExPlayerCharacter*>(this);

    return nullptr;
}

extern thread_local bool g_forceAnimation;

void Actor::SetWeaponDrawnEx(bool aDraw) noexcept
{
    spdlog::debug("Setting weapon drawn: {:X}:{}, current state: {}", formID, aDraw, actorState.IsWeaponDrawn());

    // A copy's own draw/sheathe actions are refused, so a request the engine made for it can stay
    // pending (1 wants to draw, 4 wants to sheathe) and swallow every new one: settle it back to
    // what the actor shows first.
    if (actorState.GetWeaponState() == 1)
        actorState.SetWeaponStateFlags(0);
    else if (actorState.GetWeaponState() == 4)
        actorState.SetWeaponStateFlags(3);

    // Already sheathed: nothing to play, and the toggle below could strand the flag at "drawn".
    if (!aDraw && !actorState.IsWeaponDrawn())
        return;

    // The game ignores a draw for an actor already flagged drawn, so flip the flag first to make
    // it play the draw again.
    if (actorState.IsWeaponDrawn() == aDraw)
    {
        actorState.SetWeaponDrawn(!aDraw);

        spdlog::debug("Setting weapon drawn after update: {:X}:{}, current state: {}", formID, aDraw, actorState.IsWeaponDrawn());
    }

    g_forceAnimation = true;
    SetWeaponDrawn(aDraw);
    g_forceAnimation = false;
}

void Actor::SetPlayerRespawnMode(bool aSet) noexcept
{
    SetEssentialEx(aSet);
    // Makes the player go in an unrecoverable bleedout state
    SetNoBleedoutRecovery(aSet);

    if (formID != 0x14)
    {
        auto pPlayerFaction = Cast<TESFaction>(TESForm::GetById(0xDB1));
        SetFactionRank(pPlayerFaction, 1);
    }
}

void Actor::SetEssentialEx(bool aSet) noexcept
{
    SetEssential(aSet);
    TESNPC* pBase = Cast<TESNPC>(baseForm);
    if (pBase)
        pBase->actorData.SetEssential(aSet);
}

void Actor::SetNoBleedoutRecovery(bool aSet) noexcept
{
    using TSetNoBleedoutRecovery = void(Actor*, bool);
    POINTER_SKYRIMSE(TSetNoBleedoutRecovery, s_setNoBleedoutRecovery, 38533);
    s_setNoBleedoutRecovery(this, aSet);
}

void Actor::DispelAllSpells(bool aNow) noexcept
{
    magicTarget.DispelAllSpells(aNow);
}

bool Actor::IsInCombat() const noexcept
{
    PAPYRUS_FUNCTION(bool, Actor, IsInCombat);
    return s_pIsInCombat(this);
}

Actor* Actor::GetCombatTarget() const noexcept
{
    PAPYRUS_FUNCTION(Actor*, Actor, GetCombatTarget);
    return s_pGetCombatTarget(this);
}

// TODO: this is a really hacky solution.
// The internal targeting system should be disabled instead.
void Actor::StartCombatEx(Actor* apTarget) noexcept
{
    if (GetCombatTarget() != apTarget)
    {
        StopCombat();
        StartCombat(apTarget);
    }
}

void Actor::SetCombatTargetEx(Actor* apTarget) noexcept
{
    if (pCombatController)
        pCombatController->SetTarget(apTarget);
}

void Actor::StartCombat(Actor* apTarget) noexcept
{
    PAPYRUS_FUNCTION(void, Actor, StartCombat, Actor*);
    s_pStartCombat(this, apTarget);
}

void Actor::StopCombat() noexcept
{
    PAPYRUS_FUNCTION(void, Actor, StopCombat);
    s_pStopCombat(this);
}

bool Actor::AddSpell(SpellItem* apSpell) noexcept
{
    using TAddSpell = bool(Actor*, SpellItem*);
    POINTER_SKYRIMSE(TAddSpell, s_addSpell, 38716);
    return apSpell && s_addSpell(this, apSpell);
}

void Actor::SwitchRace(TESRace* apRace, bool aPlayer) noexcept
{
    using TSwitchRace = void(Actor*, TESRace*, bool);
    POINTER_SKYRIMSE(TSwitchRace, s_switchRace, 37925);
    if (apRace)
        s_switchRace(this, apRace, aPlayer);
}

bool Actor::RemoveSpell(MagicItem* apSpell) noexcept
{
    if (!apSpell)
    {
        spdlog::error("{}: : apSpell is null", __FUNCTION__);
        return false;
    }
    // spdlog::info("{}: : removing: {} from actor: {}", __FUNCTION__, apSpell->formID, formID);
    return RealRemoveSpell(this, apSpell);
}

bool Actor::HasPerk(uint32_t aPerkFormId) const noexcept
{
    return GetPerkRank(aPerkFormId) != 0;
}

uint8_t Actor::GetPerkRank(uint32_t aPerkFormId) const noexcept
{
    BGSPerk* pPerk = Cast<BGSPerk>(TESForm::GetById(aPerkFormId));
    if (!pPerk)
        return 0;

    using TGetPerkRank = uint8_t(const Actor*, BGSPerk*);
    POINTER_SKYRIMSE(TGetPerkRank, getPerkRank, 37698);

    return getPerkRank(this, pPerk);
}

bool HookRemoveSpell(Actor* apThis, MagicItem* apSpell)
{
    bool result = RealRemoveSpell(apThis, apSpell);
    if (apThis->GetExtension()->IsLocalPlayer() && result)
    {
        //spdlog::info("{}: : spell: {}, ID: {} from local player", __FUNCTION__, apSpell->GetName() , apSpell->formID);
       RemoveSpellEvent removalEvent;

        removalEvent.TargetId = apThis->formID;
        removalEvent.SpellId = apSpell->formID;
        World::Get().GetRunner().Trigger(removalEvent);
    }

    return result;
}

Actor* HookCharacterConstructor(Actor* apThis)
{
    EMPTY_HOOK_PLACEHOLDER;

    RealCharacterConstructor(apThis);

    return apThis;
}

Actor* HookCharacterConstructor2(Actor* apThis, uint8_t aUnk)
{
    EMPTY_HOOK_PLACEHOLDER;

    RealCharacterConstructor2(apThis, aUnk);

    return apThis;
}

Actor* HookCharacterDestructor(Actor* apThis)
{
    EMPTY_HOOK_PLACEHOLDER;

    auto pExtension = apThis->GetExtension();

    if (pExtension)
    {
        pExtension->~ActorExtension();
    }

    RealCharacterDestructor(apThis);

    return apThis;
}

GamePtr<Actor> Actor::New() noexcept
{
    auto* const pActor = Memory::Allocate<Actor>();

    RealCharacterConstructor(pActor);

    return {pActor};
}

void Actor::InterruptCast(bool abRefund) noexcept
{
    using TInterruptCast = void(Actor*, bool abRefund);

    POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 38757);

    s_interruptCast(this, abRefund);
}

TESForm* Actor::GetEquippedWeapon(uint32_t aSlotId) const noexcept
{
    if (currentProcess && currentProcess->middleProcess)
    {
        auto pMiddleProcess = currentProcess->middleProcess;

        if (aSlotId == 0 && pMiddleProcess->leftEquippedObject)
            return pMiddleProcess->leftEquippedObject->pObject;

        else if (aSlotId == 1 && pMiddleProcess->rightEquippedObject)
            return pMiddleProcess->rightEquippedObject->pObject;
    }

    return nullptr;
}

TESForm* Actor::GetEquippedAmmo() const noexcept
{
    if (currentProcess && currentProcess->middleProcess && currentProcess->middleProcess->ammoEquippedObject)
    {
        // TODO: rtti cast to check if is ammo object? or actually, just call AIProcess::GetCurrentAmmo()
        return currentProcess->middleProcess->ammoEquippedObject->pObject;
    }

    return nullptr;
}

bool Actor::IsWearingBodyPiece() const noexcept
{
    return GetContainerChanges()->GetArmor(32) != nullptr;
}

bool Actor::ShouldWearBodyPiece() const noexcept
{
    TESNPC* pBase = Cast<TESNPC>(baseForm);
    if (!pBase)
        return false;

    BGSOutfit* pDefaultOutfit = pBase->outfits[0];
    if (!pDefaultOutfit)
        return false;

    for (auto* pItem : pDefaultOutfit->outfitItems)
    {
        TESObjectARMO* pArmor = nullptr;

        if (pItem->formType == FormType::Armor)
            pArmor = Cast<TESObjectARMO>(pItem);
        else if (pItem->formType == FormType::LeveledItem)
        {
            TESLevItem* pLevItem = Cast<TESLevItem>(pItem);
            if (!pLevItem || !pLevItem->pLeveledListA || !pLevItem->pLeveledListA->pForm)
                continue;

            pArmor = Cast<TESObjectARMO>(pLevItem->pLeveledListA->pForm);
        }
        else
            continue;

        if (!pArmor)
            continue;

        if (pArmor->IsBodyPiece()) 
            return true;
    }

    return false;
}

// Get owner of a summon or raised corpse
Actor* Actor::GetCommandingActor() const noexcept
{
    if (currentProcess && currentProcess->middleProcess && currentProcess->middleProcess->commandingActor)
    {
        auto handle = currentProcess->middleProcess->commandingActor.handle;
        auto* pOwner = Cast<Actor>(TESObjectREFR::GetByHandle(handle.iBits));
        return pOwner;
    }

    return nullptr;
}

// Get owner of a summon or raised corpse
void Actor::SetCommandingActor(BSPointerHandle<TESObjectREFR> aCommandingActor) noexcept
{
    if (currentProcess && currentProcess->middleProcess)
    {
        currentProcess->middleProcess->commandingActor = aCommandingActor;
        flags2 |= ActorFlags::IS_COMMANDED_ACTOR;
    }
}

bool Actor::IsPlayerSummon() const noexcept
{
    const Actor* pCommandingActor = GetCommandingActor();
    return pCommandingActor && pCommandingActor->formID == 0x14;
}

TESForm* Actor::GetCurrentLocation()
{
    // we use the safe function which also
    // checks the form type
    return FUNC_GetActorLocation(this);
}

Factions Actor::GetFactions() const noexcept
{
    Factions result;

    auto* pNpc = Cast<TESNPC>(baseForm);
    if (pNpc)
    {
        auto& factions = pNpc->actorData.factions;

        for (auto i = 0u; i < factions.length; ++i)
        {
            Faction faction;

            faction.Id = ToSession(factions[i].faction->formID).value_or(GameId{});
            faction.Rank = factions[i].rank;

            result.NpcFactions.push_back(faction);
        }
    }

    auto* pChanges = Cast<ExtraFactionChanges>(extraData.GetByType(ExtraDataType::Faction));
    if (pChanges)
    {
        for (auto i = 0u; i < pChanges->entries.length; ++i)
        {
            Faction faction;

            faction.Id = ToSession(pChanges->entries[i].faction->formID).value_or(GameId{});
            faction.Rank = pChanges->entries[i].rank;

            result.ExtraFactions.push_back(faction);
        }
    }

    return result;
}

ActorValues Actor::GetEssentialActorValues() const noexcept
{
    ActorValues actorValues;

    int essentialValues[] = {ActorValueInfo::kHealth, ActorValueInfo::kStamina, ActorValueInfo::kMagicka};
    for (auto i : essentialValues)
    {
        float value = EngineVirtual(actorValueOwner).GetValue(i);
        actorValues.ActorValuesList.insert({i, value});
        float maxValue = EngineVirtual(actorValueOwner).GetPermanentValue(i);
        actorValues.ActorMaxValuesList.insert({i, maxValue});
    }

    return actorValues;
}

float Actor::GetActorValue(uint32_t aId) const noexcept
{
    return EngineVirtual(actorValueOwner).GetValue(aId);
}

float Actor::GetActorPermanentValue(uint32_t aId) const noexcept
{
    return EngineVirtual(actorValueOwner).GetPermanentValue(aId);
}

void Actor::SetActorValue(uint32_t aId, float aValue) noexcept
{
    EngineVirtual(actorValueOwner).SetValue(aId, aValue);
}

void Actor::ForceActorValue(ActorValueOwner::ForceMode aMode, uint32_t aId, float aValue) noexcept
{
    float initialValue = aMode == ActorValueOwner::ForceMode::PERMANENT 
                         ? GetActorPermanentValue(aId)
                         : GetActorValue(aId);

    if (aValue == initialValue)
        return;

    EngineVirtual(actorValueOwner).ForceCurrent(aMode, aId, aValue - initialValue);
}

void Actor::DamageHealth(float aDamage, Actor* apAttacker) noexcept
{
    // Actor::SetActorValueModifiers (38468), as DoDamage (37335) calls it: calls HandleHealthDamage (38606).
    using TSetActorValueModifiers = void(Actor*, int32_t aModifier, uint32_t aId, float aValue, Actor* apSource);
    POINTER_SKYRIMSE(TSetActorValueModifiers, s_setActorValueModifiers, 38468);
    constexpr int32_t cDamageModifier = 2;
    (s_setActorValueModifiers.Get())(this, cDamageModifier, ActorValueInfo::kHealth, -aDamage, apAttacker);
}

Inventory Actor::GetActorInventory() const noexcept
{
    Inventory inventory = GetInventory();

    inventory.CurrentMagicEquipment = GetMagicEquipment();

    return inventory;
}

MagicEquipment Actor::GetMagicEquipment() const noexcept
{
    MagicEquipment equipment;

    uint32_t mainId = magicItems[0] ? magicItems[0]->formID : 0;
    equipment.LeftHandSpell = ToSession(mainId).value_or(GameId{});

    uint32_t secondaryId = magicItems[1] ? magicItems[1]->formID : 0;
    equipment.RightHandSpell = ToSession(secondaryId).value_or(GameId{});

    uint32_t shoutId = equippedShout ? equippedShout->formID : 0;
    equipment.Shout = ToSession(shoutId).value_or(GameId{});

    return equipment;
}

Inventory Actor::GetEquipment() const noexcept
{
    Inventory inventory = GetInventory();
    inventory.RemoveByFilter([](const auto& entry) { return !entry.IsWorn(); });
    inventory.CurrentMagicEquipment = GetMagicEquipment();
    return inventory;
}

int32_t Actor::GetGoldAmount() const noexcept
{
    using TGetGoldAmount = int32_t(const Actor*);
    POINTER_SKYRIMSE(TGetGoldAmount, s_getGoldAmount, 37527);
    return s_getGoldAmount(this);
}

void Actor::SetActorInventory(const Inventory& acInventory) noexcept
{
    spdlog::info("Setting inventory for actor {:X}", formID);

    // The UnEquipAll() that used to be here is redundant,
    // as RemoveAllItems() unequips every item if needed.
    // Placing this UnEquipAll() here seems to trigger a Skyrim bug/race.

    Inventory currentInventory = GetActorInventory();

    if (!this->GetExtension()->IsPlayer() && currentInventory.ContainsQuestItems())
        SetInventoryRetainingQuestItems(currentInventory, acInventory);
    else
        SetInventory(acInventory);

    SetMagicEquipment(acInventory.CurrentMagicEquipment);
}

void Actor::SetMagicEquipment(const MagicEquipment& acEquipment) noexcept
{
    auto* pEquipManager = EquipManager::Get();

    if (acEquipment.LeftHandSpell)
    {
        uint32_t mainHandWeaponId = ToGame(acEquipment.LeftHandSpell);
        spdlog::debug("Setting left hand spell: {:X}", mainHandWeaponId);
        pEquipManager->EquipSpell(this, TESForm::GetById(mainHandWeaponId), 0);
    }

    if (acEquipment.RightHandSpell)
    {
        uint32_t secondaryHandWeaponId = ToGame(acEquipment.RightHandSpell);
        spdlog::debug("Setting right hand spell: {:X}", secondaryHandWeaponId);
        pEquipManager->EquipSpell(this, TESForm::GetById(secondaryHandWeaponId), 1);
    }

    if (acEquipment.Shout)
    {
        uint32_t shoutId = ToGame(acEquipment.Shout);
        spdlog::debug("Setting shout: {:X}", shoutId);
        pEquipManager->EquipShout(this, TESForm::GetById(shoutId));
    }
}

void Actor::SetActorValues(const ActorValues& acActorValues) noexcept
{
    for (auto& value : acActorValues.ActorMaxValuesList)
        ForceActorValue(ActorValueOwner::ForceMode::PERMANENT, value.first, value.second);

    for (auto& value : acActorValues.ActorValuesList)
        ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, value.first, value.second);
}

void Actor::SetFactions(const Factions& acFactions) noexcept
{
    RemoveFromAllFactions();

    for (auto& entry : acFactions.NpcFactions)
    {
        auto pForm = GetById(ToGame(entry.Id));
        auto pFaction = Cast<TESFaction>(pForm);
        if (pFaction)
        {
            SetFactionRank(pFaction, entry.Rank);
        }
    }

    for (auto& entry : acFactions.ExtraFactions)
    {
        auto pForm = GetById(ToGame(entry.Id));
        auto pFaction = Cast<TESFaction>(pForm);
        if (pFaction)
        {
            SetFactionRank(pFaction, entry.Rank);
        }
    }
}

void Actor::SetFactionRank(const TESFaction* apFaction, int8_t aRank) noexcept
{
    using TSetFactionRankInternal = void(Actor*, const TESFaction*, int8_t);

    POINTER_SKYRIMSE(TSetFactionRankInternal, s_setFactionRankInternal, 37677);

    s_setFactionRankInternal(this, apFaction, aRank);
}

void Actor::SetPlayerTeammate(bool aSet) noexcept
{
    using TSetPlayerTeammate = void(Actor*, bool aSet, bool abCanDoFavor);
    POINTER_SKYRIMSE(TSetPlayerTeammate, setPlayerTeammate, 37717);
    return setPlayerTeammate(this, aSet, true);
}

void Actor::UnEquipAll() noexcept
{
    EquipManager::Get()->UnequipAll(this);

    // Taken from skyrim's code shouts can be two form types apparently
    if (equippedShout && ((int)equippedShout->formType - 41) <= 1)
    {
        EquipManager::Get()->UnEquipShout(this, equippedShout);
        equippedShout = nullptr;
    }
}

void Actor::RemoveFromAllFactions() noexcept
{
    PAPYRUS_FUNCTION(void, Actor, RemoveFromAllFactions);

    s_pRemoveFromAllFactions(this);
}

using TInitiateMountPackage = bool(Actor*, Actor* apMount);
static TInitiateMountPackage* RealInitiateMountPackage = nullptr;
using TDismount = bool(Actor*);
static TDismount* RealDismount = nullptr;

bool Actor::InitiateMountPackage(Actor* apMount) noexcept
{
    return RealInitiateMountPackage(this, apMount);
}

bool Actor::PutOnMount(Actor* apMount) noexcept
{
    using TConstruct = RefrInteraction*(void* apMemory, Actor* apRider, Actor* apMount);
    using TAttach = void(RefrInteraction** appInteraction);
    POINTER_SKYRIMSE(TConstruct, s_construct, 40345); // MountInteraction::MountInteraction
    POINTER_SKYRIMSE(TAttach, s_attach, 41532);       // into the rider's and the mount's ExtraInteraction
    if (!apMount)
        return false;
    RefrInteraction* pInteraction = s_construct(Memory::Allocate(0x38), this, apMount);
    InterlockedIncrement(&pInteraction->refCount);
    s_attach(&pInteraction);
    if (InterlockedDecrement(&pInteraction->refCount) == 0)
        delete pInteraction;
    return true;
}

void Actor::GenerateMagicCasters() noexcept
{
    using CS = MagicSystem::CastingSource;

    for (int i = 0; i < 4; i++)
    {
        if (casters[i] == nullptr)
            casters[i] = Cast<ActorMagicCaster>(GetMagicCaster(static_cast<CS>(i)));
    }
}

bool Actor::IsDead() const noexcept
{
    PAPYRUS_FUNCTION(bool, Actor, IsDead);

    return s_pIsDead(this);
}

bool Actor::IsDragon() const noexcept
{
    const ActorExtension* pExtension = const_cast<Actor*>(this)->GetExtension();
    return BehaviorVar::IsDragon(pExtension->GraphDescriptorHash);
}

void Actor::StartDeferredKill() noexcept
{
    // Character::StartDeferredKill (38616), with the papyrus wrapper's argument.
    using TStartDeferredKill = void(Actor*, bool aScripted);
    POINTER_SKYRIMSE(TStartDeferredKill, s_startDeferredKill, 38616);
    (s_startDeferredKill.Get())(this, true);
}

void Actor::EndDeferredKill() noexcept
{
    // Actor::EndDeferredKill (38618); a kill it held back is queued (task queue), after any kill made now.
    using TEndDeferredKill = void(Actor*, bool aScripted);
    POINTER_SKYRIMSE(TEndDeferredKill, s_endDeferredKill, 38618);
    (s_endDeferredKill.Get())(this, true);
}

bool Actor::IsInKillMove() const noexcept
{
    // Actor::IsInKillMove (38615): either half of a kill move.
    using TIsInKillMove = bool(const Actor*);
    POINTER_SKYRIMSE(TIsInKillMove, s_isInKillMove, 38615);
    return (s_isInKillMove.Get())(this);
}

bool Actor::IsHeldByKillMove() const noexcept
{
    // Character 38619 (in a deferred kill, as KillActorHandler tests it): a kill move holds its victim so until KillActor.
    using TIsInDeferredKill = bool(const Actor*);
    POINTER_SKYRIMSE(TIsInDeferredKill, s_isInDeferredKill, 38619);
    return IsInKillMove() && (s_isInDeferredKill.Get())(this);
}

void Actor::Kill() noexcept
{
    // Never kill players
    ActorExtension* pExtension = GetExtension();
    if (pExtension->IsPlayer())
        return;

    EndDeferredKill();

    // TODO: these args are kind of bogus of course
    KillImpl(nullptr, 100.f, true, true);

    // Papyrus kill will not go through if it is queued by a kill move
    /*
    PAPYRUS_FUNCTION(void, Actor, Kill, void*);
    s_pKill(this, NULL);
    */
}

bool Actor::KillWithDeathAction(const ActionEvent& acAction) noexcept
{
    ActorExtension* pExtension = GetExtension();
    if (pExtension->IsPlayer())
        return false;

    // KillImpl (37896) performs the death animation, or RagdollInstant when told to ragdoll at once; a
    // copy's own actions are refused unless forced.
    const bool cRagdoll = SameName(acAction.EventName, "RagdollInstant");
    const ActionEvent cPrevious = pExtension->LatestAnimation;
    pExtension->LatestAnimation.ActionId = 0;
    EndDeferredKill();
    g_forceAnimation = true;
    KillImpl(nullptr, 100.f, true, cRagdoll);
    g_forceAnimation = false;
    World::Get().GetRunner().Trigger(DeathActionReplayedEvent{formID, acAction.Tick});
    if (pExtension->LatestAnimation.ActionId == acAction.ActionId)
        return true;
    pExtension->LatestAnimation = cPrevious;
    return false;
}

void Actor::Reset() noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, Reset, int, TESObjectREFR*);

    s_pReset(this, 0, nullptr);
}

bool Actor::PlayIdle(TESIdleForm* apIdle) noexcept
{
    PAPYRUS_FUNCTION(bool, Actor, PlayIdle, TESIdleForm*);
    return s_pPlayIdle(this, apIdle);
}

void Actor::Respawn() noexcept
{
    Resurrect(false);
    Reset();
}

bool Actor::IsVampireLord() const noexcept
{
    return race && race->formID == 0x200283A;
}

extern thread_local bool g_forceAnimation;

void Actor::FixVampireLordModel() noexcept
{
    TESBoundObject* pObject = Cast<TESBoundObject>(TESForm::GetById(0x2011a84));
    if (!pObject)
        return;

    {
        ScopedInventoryOverride _;
        AddObjectToContainer(pObject, nullptr, 1, nullptr);
    }

    EquipManager::Get()->Equip(this, pObject, nullptr, 1, nullptr, false, true, false, false);

    g_forceAnimation = true;

    BSFixedString str("isLevitating");
    uint32_t isLevitating = GetAnimationVariableInt(&str);
    spdlog::critical("isLevitating {}", isLevitating);

    // By default, a loaded vampire lord is not levitating.
    if (isLevitating)
    {
        BSFixedString levitation("LevitationToggle");
        SendAnimationEvent(&levitation);
    }

    // TODO: weapon draw code does not seem to take care of this
    //BSFixedString weapEquip("WeapEquip");
    //SendAnimationEvent(&weapEquip);

    g_forceAnimation = false;
}

char HookSetPosition(Actor* apThis, NiPoint3& aPosition)
{
    const auto pExtension = apThis ? apThis->GetExtension() : nullptr;
    const auto bIsCopy = pExtension && pExtension->IsCopy();

    if (bIsCopy && !ScopedReferencesOverride::IsOverriden())
        return 1;

    // A non-actor's move reaches the change funnel (RecordMove).
    if (apThis->formType != Actor::Type)
    {
        const NiPoint3 cBefore = apThis->position;
        const char cResult = RealSetPosition(apThis, aPosition);
        apThis->RecordMove(apThis->position != cBefore);
        return cResult;
    }

    // Don't interfere with the player, or if we are calling our self
    if (apThis == PlayerCharacter::Get() || ScopedReferencesOverride::IsOverriden())
        return RealSetPosition(apThis, aPosition);

    ScopedReferencesOverride recursionGuard;

    // It just works TM
    apThis->SetPosition(aPosition, false);

    return 1;
}

using TForceState = void(Actor*, const NiPoint3&, float, float, TESObjectCELL*, TESWorldSpace*, bool);
static TForceState* RealForceState = nullptr;

void HookForceState(Actor* apThis, const NiPoint3& acPosition, float aX, float aZ, TESObjectCELL* apCell, TESWorldSpace* apWorldSpace, bool aUnkBool)
{
    /*const auto pNpc = Cast<TESNPC>(apThis->baseForm);
    if (pNpc)
    {
        spdlog::info("For TESNPC: {}, spawn at {} {} {}", pNpc->fullName.value, apPosition->m_x, apPosition->m_y,
                     apPosition->m_z);
    }*/

    // if (apThis != PlayerCharacter::Get())
    //     return;

    return RealForceState(apThis, acPosition, aX, aZ, apCell, apWorldSpace, aUnkBool);
}

using TDamageActor = bool(Actor*, float aDamage, Actor* apHitter, bool aKillMove);
static TDamageActor* RealDamageActor = nullptr;

// TODO: this is flawed, since it does not account for invulnerable actors
// Set while an active effect applies its value (HookApplyActorEffect): damage then comes from an
// effect this game owns, never from a replayed melee swing.
static thread_local bool s_applyingEffect = false;

bool HookDamageActor(Actor* apThis, float aDamage, Actor* apHitter, bool aKillMove)
{
    if (apHitter)
        World::Get().GetRunner().Trigger(HitEvent(apHitter->formID, apThis->formID));

    float realDamage = GameplayFormulas::CalculateRealDamage(apThis, aDamage, aKillMove);

    float currentHealth = apThis->GetActorValue(ActorValueInfo::kHealth);
    bool wouldKill = (currentHealth - realDamage) <= 0.f;

    const auto* pExHittee = apThis->GetExtension();
    if (pExHittee->IsLocalPlayer())
    {
        if (apHitter && apHitter->GetExtension()->IsOtherPlayer())
            return false;

        World::Get().GetRunner().Trigger(HealthChangeEvent(apThis->formID, -realDamage));
        return RealDamageActor(apThis, aDamage, apHitter, aKillMove);
    }
    else if (pExHittee->IsOtherPlayer())
    {
        return wouldKill;
    }

    if (apHitter)
    {
        const auto* pExHitter = apHitter->GetExtension();
        if (pExHitter->IsLocalPlayer())
        {
            World::Get().GetRunner().Trigger(HealthChangeEvent(apThis->formID, -realDamage, apHitter->formID));
            // Direct authority: a hit on an actor someone else owns (e.g. a host NPC) is only a
            // request to that owner. Our copy mirrors the owner's health instead of applying it.
            if (pExHittee->IsCopy())
                return wouldKill;
            return RealDamageActor(apThis, aDamage, apHitter, aKillMove);
        }
        if (pExHitter->IsOtherPlayer())
        {
            // Our actor, damaged by an effect we applied at that player's request (see HookAddTarget):
            // apply it with them as the attacker, so the game handles aggro and kill credit.
            if (s_applyingEffect && pExHittee->IsLocal())
            {
                World::Get().GetRunner().Trigger(HealthChangeEvent(apThis->formID, -realDamage));
                return RealDamageActor(apThis, aDamage, apHitter, aKillMove);
            }
            // Otherwise their replayed swing hit our actor; their game already sent the request.
            return wouldKill;
        }
    }

    if (pExHittee->IsLocal())
    {
        World::Get().GetRunner().Trigger(HealthChangeEvent(apThis->formID, -realDamage));
        return RealDamageActor(apThis, aDamage, apHitter, aKillMove);
    }
    else
    {
        return wouldKill;
    }
}

using TApplyActorEffect = void(ActiveEffect*, Actor* apTarget, float aEffectValue, unsigned int unk1);
static TApplyActorEffect* RealApplyActorEffect = nullptr;

void HookApplyActorEffect(ActiveEffect* apThis, Actor* apTarget, float aEffectValue, unsigned int unk1)
{
    struct ScopedEffect
    {
        ScopedEffect() { s_applyingEffect = true; }
        ~ScopedEffect() { s_applyingEffect = false; }
    } scope;

    const auto* pValueModEffect = Cast<ValueModifierEffect>(apThis);

    if (pValueModEffect)
    {
        if (pValueModEffect->actorValueIndex == ActorValueInfo::kHealth && aEffectValue > 0.0f)
        {
            if (apTarget && apTarget->GetExtension())
            {
                const auto pExTarget = apTarget->GetExtension();
                if (pExTarget->IsLocal())
                {
                    World::Get().GetRunner().Trigger(HealthChangeEvent(apTarget->formID, aEffectValue));
                    return RealApplyActorEffect(apThis, apTarget, aEffectValue, unk1);
                }
                return;
            }
        }
    }

    return RealApplyActorEffect(apThis, apTarget, aEffectValue, unk1);
}

// The passive regeneration of health (24), magicka (25) and stamina (26): aDelta seconds' worth, restored
// into the damage modifier. The per-value updates (38451 health, 38452 stamina, 38453 magicka) call it every
// frame. Copies take these values from their owner, so they never regenerate. (Upstream hooked 37448,
// Actor::GetSubmergedLevel, here since its 2021 AE port; see docs/re/systems/actor-values.md.)
using TRegenerateActorValue = void(Actor*, int32_t aActorValue, float aDelta);
static TRegenerateActorValue* RealRegenerateActorValue = nullptr;

void HookRegenerateActorValue(Actor* apThis, int32_t aActorValue, float aDelta)
{
    if (apThis->GetExtension()->IsCopy())
        return;

    RealRegenerateActorValue(apThis, aActorValue, aDelta);
}

void HookAddInventoryItem(Actor* apThis, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {

        Inventory::Entry item{};
        item.BaseId = ToSession(apItem->formID).value_or(GameId{});
        item.Count = aCount;

        if (apExtraData)
            apThis->GetItemFromExtraData(item, apExtraData);

        World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item)));
    }

    RealAddInventoryItem(apThis, apItem, apExtraData, aCount, apOldOwner);
}

void* HookPickUpObject(Actor* apThis, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {

        Inventory::Entry item{};
        item.BaseId = ToSession(apObject->baseForm->formID).value_or(GameId{});
        item.Count = aCount;

        if (apObject->GetExtraDataList())
            apThis->GetItemFromExtraData(item, apObject->GetExtraDataList());

        // This is here so that objects that are picked up on both clients, aka non temps, are synced through activation sync.
        // The inventory change event should always be sent to the server, otherwise the server inventory won't be updated.
        bool shouldUpdateClients = apObject->IsTemporary() && !ScopedActivateOverride::IsOverriden();

        World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item), false, shouldUpdateClients));
    }

    return RealPickUpObject(apThis, apObject, aCount, aUnk1, aUnk2);
}

void Actor::PickUpObject(TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2) noexcept
{
    RealPickUpObject(this, apObject, aCount, aUnk1, aUnk2);
}

void* HookDropObject(Actor* apThis, void* apResult, TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation)
{

    Inventory::Entry item{};
    item.BaseId = ToSession(apObject->formID).value_or(GameId{});
    item.Count = -aCount;

    if (apExtraData)
        apThis->GetItemFromExtraData(item, apExtraData);

    World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item), true));

    ScopedInventoryOverride _;

    return RealDropObject(apThis, apResult, apObject, apExtraData, aCount, apLocation, apRotation);
}

void Actor::DropOrPickUpObject(const Inventory::Entry& arEntry, NiPoint3* apLocation, NiPoint3* apRotation) noexcept
{
    ExtraDataList* pExtraData = GetExtraDataFromItem(arEntry);

    uint32_t objectId = ToGame(arEntry.BaseId);
    TESBoundObject* pObject = Cast<TESBoundObject>(TESForm::GetById(objectId));
    if (!pObject)
    {
        spdlog::warn("Object to drop not found, {:X}:{:X}.", arEntry.BaseId.ModId, arEntry.BaseId.BaseId);
        return;
    }

    if (arEntry.Count < 0)
        DropObject(pObject, pExtraData, -arEntry.Count, apLocation, apRotation);
    // TODO: pick up
}

void Actor::DropObject(TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation) noexcept
{
    spdlog::debug("Dropping object, form id: {:X}, count: {}, actor: {:X}", apObject->formID, aCount, formID);
    BSPointerHandle<TESObjectREFR> result{};
    RealDropObject(this, &result, apObject, apExtraData, aCount, apLocation, apRotation);
}

using TUpdateDetectionState = void(ActorKnowledge*, void*);
static TUpdateDetectionState* RealUpdateDetectionState = nullptr;

void HookUpdateDetectionState(ActorKnowledge* apThis, void* apState)
{
    auto pOwner = TESObjectREFR::GetByHandle(apThis->hOwner);
    auto pTarget = TESObjectREFR::GetByHandle(apThis->hTarget);

    if (pOwner && pTarget)
    {
        auto pOwnerActor = Cast<Actor>(pOwner);
        auto pTargetActor = Cast<Actor>(pTarget);
        if (pOwnerActor && pTargetActor)
        {
            if (pOwnerActor->GetExtension()->IsOtherPlayer() && pTargetActor->GetExtension()->IsLocalPlayer())
            {
                spdlog::debug("Cancelling detection from remote player to local player, owner: {:X}, target: {:X}", pOwner->formID, pTarget->formID);
                return;
            }
        }
    }

    return RealUpdateDetectionState(apThis, apState);
}

struct DialogueItem;

// TODO: This is an AIProcess function
// The engine passes a fifth argument (a byte it hands on to the response's processing); dropping it left
// the original reading an uninitialized stack slot.
using TProcessResponse = uint64_t(void*, DialogueItem* apVoice, Actor* apTalkingActor, Actor* apTalkedToActor, bool aUnk);
static TProcessResponse* RealProcessResponse = nullptr;

uint64_t HookProcessResponse(void* apThis, DialogueItem* apVoice, Actor* apTalkingActor, Actor* apTalkedToActor, bool aUnk)
{
    if (apTalkingActor)
    {
        if (apTalkingActor->GetExtension()->IsOtherPlayer())
            return 0;
    }
    return RealProcessResponse(apThis, apVoice, apTalkingActor, apTalkedToActor, aUnk);
}

bool HookInitiateMountPackage(Actor* apThis, Actor* apMount)
{
    if (apMount && apThis->GetExtension()->IsLocal())
        World::Get().GetRunner().Trigger(MountEvent(apThis->formID, apMount->formID));

    return RealInitiateMountPackage(apThis, apMount);
}

// Every dismount starts here: the player's activate, Papyrus Dismount, the console, the get-up package.
bool HookDismount(Actor* apThis)
{
    const bool cStarted = RealDismount(apThis);
    if (cStarted && apThis->GetExtension()->IsLocal())
        World::Get().GetRunner().Trigger(DismountEvent{apThis->formID});
    return cStarted;
}

using TUnequipObject = void(Actor*, void* apUnk1, TESBoundObject* apObject, int32_t aUnk2, void* apUnk3);
static TUnequipObject* RealUnequipObject = nullptr;

void HookUnequipObject(Actor* apThis, void* apUnk1, TESBoundObject* apObject, int32_t aUnk2, void* apUnk3)
{
    RealUnequipObject(apThis, apUnk1, apObject, aUnk2, apUnk3);
}

// Character::SpeakSound (37542): queues a line; returns when it ends (game seconds), -1 without a voice, which
// Actor::UpdateInDialogue (37541) takes as said: the menu's voice is ready and it takes a pick.
using TSpeakSoundFunction = float(Actor*, const char* apName, uint32_t* a3, uint32_t a4, uint32_t a5, uint32_t a6, uint64_t a7, uint64_t a8, uint64_t a9, bool a10, uint64_t a11, bool a12, bool a13, bool a14);
static TSpeakSoundFunction* RealSpeakSoundFunction = nullptr;

float HookSpeakSoundFunction(Actor* apThis, const char* apName, uint32_t* a3, uint32_t a4, uint32_t a5, uint32_t a6, uint64_t a7, uint64_t a8, uint64_t a9, bool a10, uint64_t a11, bool a12, bool a13, bool a14)
{
    spdlog::debug("a3: {:X}, a4: {}, a5: {}, a6: {}, a7: {}, a8: {:X}, a9: {:X}, a10: {}, a11: {:X}, a12: {}, a13: {}, a14: {}", (uint64_t)a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);

    if (apThis->GetExtension()->IsLocal())
        World::Get().GetRunner().Trigger(DialogueEvent(apThis->formID, apName));
    // A copy says only its owner's lines (NotifyDialogue calls past this hook), none its game picks: no voice.
    else if (apThis->GetExtension()->IsCopy())
        return -1.f;

    return RealSpeakSoundFunction(apThis, apName, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
}

void Actor::SpeakSound(const char* pFile)
{
    uint32_t handle[3]{};
    handle[0] = -1;
    RealSpeakSoundFunction(this, pFile, handle, 0, 0x32, 0, 0, 0, 0, 0, 0, 0, 1, 1);
}

// Actor::ResetInventory (37322): back to its base's template, outfit taken off and put on again, as enabling
// it does (TESObjectREFR::Enable 19800). A copy's inventory is its owner's: never reset (copy-contract.md).
using TResetInventory = void(Actor*, bool aLeveledOnly);
static TResetInventory* RealResetInventory = nullptr;
void HookResetInventory(Actor* apThis, bool aLeveledOnly)
{
    if (apThis->GetExtension()->IsCopy())
        return;
    RealResetInventory(apThis, aLeveledOnly);
}

// Actor::ApplyMovement (37356): the sync positions copies.
void HookApplyMovement(Actor* apThis, float aDelta)
{
    if (apThis->GetExtension()->IsCopy())
        return;

    RealApplyMovement(apThis, aDelta);
}

// Character::UpdateAI (37347). Copies skip it but run its body upkeep below
// (docs/re/systems/actor-ai-and-copies.md).
using TUpdateAI = void(Actor*, float aDelta);
static TUpdateAI* RealUpdateAI = nullptr;
// Character::UpdateCellAttachment (37352): loads/unloads 3D as the actor's cell attaches.
using TUpdateCellAttachment = void(Actor*);
static TUpdateCellAttachment* UpdateCellAttachment = nullptr;
// Character::UpdateVoiceTimer (37752): counts the voice line down and marks it ended.
using TUpdateVoiceTimer = void(Actor*, bool aForceEnd);
static TUpdateVoiceTimer* UpdateVoiceTimer = nullptr;
// Character::CalculateEssentialProtected (37227): kEssential/kProtected.
using TCalculateEssentialProtected = void(Actor*);
static TCalculateEssentialProtected* CalculateEssentialProtected = nullptr;
// AIProcess::GetLipData (39373), AIProcess::UpdateLipSyncGraph (39377): mouth movement of races
// without a FaceGen head (dragons).
using TGetLipData = void*(AIProcess*);
static TGetLipData* GetLipData = nullptr;
using TUpdateLipSyncGraph = void(AIProcess*, void* apLipData, Actor* apActor);
static TUpdateLipSyncGraph* UpdateLipSyncGraph = nullptr;

void HookUpdateAI(Actor* apThis, float aDelta)
{
    if (apThis->GetExtension()->IsCopy())
    {
        // UpdateAI's own early return.
        if (!(apThis->flags1 & Actor::kProcessMe) || apThis->IsDisabled())
            return;

        UpdateVoiceTimer(apThis, false);
        apThis->UpdateSoundCallback(false);

        // Copies die with their owner, never from their own health (as skymp keeps its remote actors).
        if (!apThis->IsDead())
            apThis->StartDeferredKill();

        AIProcess* pProcess = apThis->currentProcess;
        if (pProcess && pProcess->processLevel == 0)
        {
            CalculateEssentialProtected(apThis);
            if (void* pLipData = GetLipData(pProcess); pLipData && apThis->race && !(apThis->race->raceFlags & TESRace::kFaceGenHead))
                UpdateLipSyncGraph(pProcess, pLipData, apThis);
        }

        // UpdateAI does this last, below high process.
        if (apThis->baseForm && (!pProcess || pProcess->processLevel != 0))
            UpdateCellAttachment(apThis);
        return;
    }

    RealUpdateAI(apThis, aDelta);
}

using TAddDeathItems = void(Actor*);
static TAddDeathItems* RealAddDeathItems = nullptr;

void HookAddDeathItems(Actor* apThis)
{
    if (apThis->GetExtension()->IsCopy())
        return;

    RealAddDeathItems(apThis);
}

using TIsFleeing = bool(Actor*);
static TIsFleeing* RealIsFleeing = nullptr;

bool HookIsFleeing(Actor* apThis)
{
    // TODO: Player or RemotePlayer? Can players be in fleeing mode in skyrim?
    // TODO: investigate why the flee flag is set at all on remote players sometimes.
    if (apThis->GetExtension()->IsPlayer())
        return false;
    
    return RealIsFleeing(apThis);
}

using TIsHostileToActor = bool(Actor*, Actor* apOther);
static TIsHostileToActor* RealIsHostileToActor = nullptr;

namespace
{
// Who is angry with which remote player's character: key = actor form id << 32 | character form
// id. The hooks below run on the engine's AI worker threads, often while it holds reference locks,
// so they must never block: they only read these atomic slots. Only the main thread writes them.
constexpr size_t cAngerSlots = 32;
std::atomic<uint64_t> s_angerKeys[cAngerSlots]{};
bool s_angerFought[cAngerSlots]{}; // main thread only: has fought them since getting angry
std::atomic<size_t> s_angerCount{0};

uint64_t AngerKey(const Actor* apActor, const Actor* apOther) noexcept
{
    return (uint64_t(apActor->formID) << 32) | apOther->formID;
}
} // namespace

void MarkAngryWithRemotePlayer(Actor* apActor, Actor* apRemotePlayer) noexcept
{
    const uint64_t cKey = AngerKey(apActor, apRemotePlayer);
    size_t free = cAngerSlots;
    for (size_t i = 0; i < cAngerSlots; ++i)
    {
        const uint64_t cSlot = s_angerKeys[i].load(std::memory_order_relaxed);
        if (cSlot == cKey)
            return;
        if (cSlot == 0 && free == cAngerSlots)
            free = i;
    }
    if (free == cAngerSlots)
        return;
    s_angerFought[free] = false;
    s_angerKeys[free].store(cKey, std::memory_order_release);
    s_angerCount.fetch_add(1, std::memory_order_relaxed);
}

void UpdateRemotePlayerAnger() noexcept
{
    if (s_angerCount.load(std::memory_order_relaxed) == 0)
        return;

    for (size_t i = 0; i < cAngerSlots; ++i)
    {
        const uint64_t cKey = s_angerKeys[i].load(std::memory_order_relaxed);
        if (!cKey)
            continue;
        auto* pActor = Cast<Actor>(TESForm::GetById(static_cast<uint32_t>(cKey >> 32)));
        auto* pRemotePlayer = Cast<Actor>(TESForm::GetById(static_cast<uint32_t>(cKey)));
        bool resolved = !pActor || !pRemotePlayer || pActor->IsDead() || !pActor->GetNiNode();
        // Skyrim decides when the fight is over (yield, flee, death, distance...): once the actor
        // has fought them and is out of combat, the anger is resolved, like the engine's own
        // "angry with player" for its player.
        if (!resolved)
        {
            if (pActor->GetCombatTarget() == pRemotePlayer)
                s_angerFought[i] = true;
            else if (s_angerFought[i] && !pActor->IsInCombat())
                resolved = true;
        }
        if (resolved)
        {
            spdlog::info("Anger of {:X} at remote player {:X} resolved", static_cast<uint32_t>(cKey >> 32), static_cast<uint32_t>(cKey));
            s_angerKeys[i].store(0, std::memory_order_release);
            s_angerCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

static bool IsAngryWithRemotePlayer(Actor* apActor, Actor* apOther) noexcept
{
    if (!apActor || !apOther || s_angerCount.load(std::memory_order_relaxed) == 0)
        return false;
    const uint64_t cKey = AngerKey(apActor, apOther);
    for (const auto& cSlot : s_angerKeys)
        if (cSlot.load(std::memory_order_acquire) == cKey)
            return true;
    return false;
}

// Hang diagnostics: every loaded actor's reference lock (TESObjectREFR+0x88, BSReadWriteLock
// {writer thread id, lock word}); held ones are logged from the watchdog thread (read-only peeks
// of pointers refreshed by the main thread).
static std::atomic<Actor*> s_lockWatch[128]{};

static void LogHeldReferenceLocks()
{
    for (auto& slot : s_lockWatch)
        if (Actor* pActor = slot.load())
        {
            const auto* pLock = reinterpret_cast<const volatile uint32_t*>(reinterpret_cast<const uint8_t*>(pActor) + 0x88);
            if (pLock[0] || pLock[1])
                spdlog::error("HangWatchdog: actor {:X} reference lock held: writer thread {}, lock word {:#x}", pActor->formID, uint32_t(pLock[0]), uint32_t(pLock[1]));
        }
    spdlog::error("HangWatchdog: (end of held reference locks)");
    LockOrder::LogHeldLocks();
}

static bool DescribeReferenceLock(const void* apLock, char* apBuffer, size_t aSize)
{
    for (auto& slot : s_lockWatch)
        if (Actor* pActor = slot.load())
            if (reinterpret_cast<const uint8_t*>(pActor) + 0x88 == apLock)
            {
                *std::format_to_n(apBuffer, aSize - 1, "actor {:X} reference lock", pActor->formID).out = '\0';
                return true;
            }
    return false;
}

void RefreshHangLockWatch() noexcept
{
    static const bool s_diagnosticSet = [] {
        CrashHandler::SetHangDiagnostic(&LogHeldReferenceLocks);
        // Mutex::Lock1 (13129), the engine's BSSpinLock wait: the lock stays in rdi while it sleeps.
        POINTER_SKYRIMSE(void, s_spinLockWait, 13129);
        CrashHandler::SetSpinLockWait(reinterpret_cast<DWORD64>(s_spinLockWait.Get()), [](const CONTEXT& acFrame) { return acFrame.Rdi; });
        LockOrder::SetLockDescriber(&DescribeReferenceLock);
        return true;
    }();
    size_t index = 0;
    if (auto* pPlayer = Cast<Actor>(TESForm::GetById(0x14)))
        s_lockWatch[index++] = pPlayer;
    for (const uint32_t cFormId : World::Get().GetActorBridge().GetLoadedActorFormIds())
        if (index < std::size(s_lockWatch))
            if (auto* pActor = Cast<Actor>(TESForm::GetById(cFormId)))
                s_lockWatch[index++] = pActor;
    for (; index < std::size(s_lockWatch); ++index)
        s_lockWatch[index] = nullptr;
}

bool HookIsHostileToActor(Actor* apThis, Actor* apOther)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return true;
    return RealIsHostileToActor(apThis, apOther);
}

// The relationship every hostility and combat decision reads: 0 neutral, 1 enemy, 2 ally, 3 friend.
// GetFactionReaction (37666) returns the value cached in the actor's knowledge of the other, else
// computes it (37667); the cache is refreshed from 37667. An angry actor sees the remote player as
// an enemy in both.
constexpr int32_t cReactionEnemy = 1;

using TGetFactionReaction = int32_t(Actor*, Actor* apOther);
static TGetFactionReaction* RealGetFactionReaction = nullptr;

int32_t HookGetFactionReaction(Actor* apThis, Actor* apOther)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return cReactionEnemy;
    return RealGetFactionReaction(apThis, apOther);
}

// The core "may this actor attack that one" check (CanAttackActor, IsHostileToActor and combat
// use it). It reads the relationship cached in the actor's knowledge of the other directly, which
// still says ally right after the actor got angry, so answer it here too.
using TShouldAttack = bool(Actor*, Actor* apOther, int32_t aAggression, uint8_t aFlags);
static TShouldAttack* RealShouldAttack = nullptr;

bool HookShouldAttack(Actor* apThis, Actor* apOther, int32_t aAggression, uint8_t aFlags)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return true;
    return RealShouldAttack(apThis, apOther, aAggression, aFlags);
}

using TComputeReaction = int32_t(Actor*, Actor* apOther, bool aUnk);
static TComputeReaction* RealComputeReaction = nullptr;

int32_t HookComputeReaction(Actor* apThis, Actor* apOther, bool aUnk)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return cReactionEnemy;
    return RealComputeReaction(apThis, apOther, aUnk);
}

static Hooks::Initializer s_actorHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TCharacterDestructor, s_characterDtor, 37175);
        POINTER_SKYRIMSE(TGetLocation, s_GetActorLocation, 19812);

        POINTER_SKYRIMSE(TUpdateCellAttachment, s_updateCellAttachment, 37352);
        UpdateCellAttachment = s_updateCellAttachment.Get();
        POINTER_SKYRIMSE(TUpdateVoiceTimer, s_updateVoiceTimer, 37752);
        UpdateVoiceTimer = s_updateVoiceTimer.Get();
        POINTER_SKYRIMSE(TCalculateEssentialProtected, s_calculateEssentialProtected, 37227);
        CalculateEssentialProtected = s_calculateEssentialProtected.Get();
        POINTER_SKYRIMSE(TGetLipData, s_getLipData, 39373);
        GetLipData = s_getLipData.Get();
        POINTER_SKYRIMSE(TUpdateLipSyncGraph, s_updateLipSyncGraph, 39377);
        UpdateLipSyncGraph = s_updateLipSyncGraph.Get();
        FUNC_GetActorLocation = s_GetActorLocation.Get();

        HOOK_ID(37356, &RealApplyMovement, HookApplyMovement);
        HOOK_ID(37322, &RealResetInventory, HookResetInventory);
        HOOK_ID(37347, &RealUpdateAI, HookUpdateAI);
        HOOK_ID(19790, &RealSetPosition, HookSetPosition);
        HOOK_ID(38717, &RealRemoveSpell, HookRemoveSpell);
        HOOK_ID(40245, &RealCharacterConstructor, HookCharacterConstructor);
        HOOK_ID(40246, &RealCharacterConstructor2, HookCharacterConstructor2);
        HOOK_ID(37313, &RealForceState, HookForceState);
        HOOK_ID(37335, &RealDamageActor, HookDamageActor);
        HOOK_ID(35086, &RealApplyActorEffect, HookApplyActorEffect);
        HOOK_ID(38459, &RealRegenerateActorValue, HookRegenerateActorValue);
        HOOK_ID(37537, &RealIsHostileToActor, HookIsHostileToActor);
        HOOK_ID(37666, &RealGetFactionReaction, HookGetFactionReaction);
        HOOK_ID(37534, &RealShouldAttack, HookShouldAttack);
        HOOK_ID(37667, &RealComputeReaction, HookComputeReaction);
        HOOK_ID(37525, &RealAddInventoryItem, HookAddInventoryItem);
        HOOK_ID(37521, &RealPickUpObject, HookPickUpObject);
        HOOK_ID(40454, &RealDropObject, HookDropObject);
        HOOK_ID(42704, &RealUpdateDetectionState, HookUpdateDetectionState);
        HOOK_ID(39643, &RealProcessResponse, HookProcessResponse);
        HOOK_ID(37905, &RealInitiateMountPackage, HookInitiateMountPackage);
        HOOK_ID(37906, &RealDismount, HookDismount);
        HOOK_ID(37975, &RealUnequipObject, HookUnequipObject);
        HOOK_ID(37542, &RealSpeakSoundFunction, HookSpeakSoundFunction);
        HOOK_ID(37198, &RealAddDeathItems, HookAddDeathItems);
        HOOK_ID(37577, &RealIsFleeing, HookIsFleeing);
    });
