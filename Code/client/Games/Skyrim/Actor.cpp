#include <CrashHandler.h>
#include <HookSwitch.h>
#include <BSCore/LockOrder.h>
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
#include <Forms/TESLevItem.h>
#include <Combat/CombatController.h>

#include <Events/HealthChangeEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/MountEvent.h>
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

#ifdef SAVE_STUFF

#include <Games/Skyrim/SaveLoad.h>
#include "Actor.h"

void Actor::Save_Reversed(const uint32_t aChangeFlags, Buffer::Writer& aWriter)
{
    BGSSaveFormBuffer buffer;

    Save(&buffer);

    AIProcess* pProcess = currentProcess;
    const int32_t handlerId = pProcess != nullptr ? pProcess->handlerId : -1;

    aWriter.WriteBytes((uint8_t*)&handlerId, 4); // TODO: is this needed ?
    aWriter.WriteBytes((uint8_t*)&flags1, 4);

    //     if (!handlerId
    //         && (uint8_t)AIProcess::GetBoolInSubStructure(pProcess))
    //     {
    //         Actor::SaveSkinFar(this);
    //     }

    TESObjectREFR::Save_Reversed(aChangeFlags, aWriter);

    if (pProcess)
        ; // Skyrim saves the process manager state, but we don't give a shit so skip !

    aWriter.WriteBytes((uint8_t*)&soundCallBackSet, 4);
    aWriter.WriteBytes((uint8_t*)&headTrackingUpdateDelay, 4);
    aWriter.WriteBytes((uint8_t*)&unk9C, 4);
    // We skip 0x180 as it's not something we care about, some timer related data

    aWriter.WriteBytes((uint8_t*)&unk98, 4);
    // skip A8 - related to timers
    // skip AC - related to timers as well
    aWriter.WriteBytes((uint8_t*)&unkB0, 4);
    // skip E4 - never seen this used
    // skip E8 - same as E4
    aWriter.WriteBytes((uint8_t*)&unk84, 4);
    aWriter.WriteBytes((uint8_t*)&unkA4, 4);
    // skip baseForm->weight
    // skip 12C

    // Save actor state sub_6F0FB0
}

#endif

TP_THIS_FUNCTION(TRemoveSpell, bool, Actor, MagicItem*);
TP_THIS_FUNCTION(TCharacterConstructor, Actor*, Actor);
TP_THIS_FUNCTION(TCharacterConstructor2, Actor*, Actor, uint8_t aUnk);
TP_THIS_FUNCTION(TCharacterDestructor, Actor*, Actor);
TP_THIS_FUNCTION(TAddInventoryItem, void, Actor, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner);
TP_THIS_FUNCTION(TPickUpObject, void*, Actor, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2);
TP_THIS_FUNCTION(TDropObject, void*, Actor, void* apResult, TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation);
TP_THIS_FUNCTION(TSetPosition, char, Actor, NiPoint3& acPosition);
TP_THIS_FUNCTION(TApplyMovement, void, Actor, float aDelta);

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
    TP_THIS_FUNCTION(TGetLevel, uint16_t, const Actor);
    POINTER_SKYRIMSE(TGetLevel, s_getLevel, 37334);
    return TiltedPhoques::ThisCall(s_getLevel, this);
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

    TP_THIS_FUNCTION(TQueueUpdate, void, Actor, bool);
    POINTER_SKYRIMSE(TQueueUpdate, QueueUpdate, 40255);

    TiltedPhoques::ThisCall(QueueUpdate, this, true);

    pSetting->data = originalValue;
}

GamePtr<Actor> Actor::Create(TESNPC* apBaseForm) noexcept
{
    auto pActor = New();
    // Prevent saving
    pActor->SetSkipSaveFlag(true);
    pActor->GetExtension()->SetRemote(true);

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
    TP_THIS_FUNCTION(TActorSetLevelMod, void, ExtraDataList, uint32_t);
    POINTER_SKYRIMSE(TActorSetLevelMod, realSetLevelMod, 11806);

    const auto pExtraDataList = &extraData;

    TiltedPhoques::ThisCall(realSetLevelMod, pExtraDataList, aLevel);
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

static thread_local bool s_execInitPackage = false;

void Actor::SetPackage(TESPackage* apPackage) noexcept
{
    s_execInitPackage = true;
    PutCreatedPackage(apPackage);
    s_execInitPackage = false;
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
    TP_THIS_FUNCTION(TSetNoBleedoutRecovery, void, Actor, bool);
    POINTER_SKYRIMSE(TSetNoBleedoutRecovery, s_setNoBleedoutRecovery, 38533);
    TiltedPhoques::ThisCall(s_setNoBleedoutRecovery, this, aSet);
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

bool Actor::RemoveSpell(MagicItem* apSpell) noexcept
{
    if (!apSpell)
    {
        spdlog::error("{}: : apSpell is null", __FUNCTION__);
        return false;
    }
    // spdlog::info("{}: : removing: {} from actor: {}", __FUNCTION__, apSpell->formID, formID);
    return TiltedPhoques::ThisCall(RealRemoveSpell, this, apSpell);
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

    TP_THIS_FUNCTION(TGetPerkRank, uint8_t, const Actor, BGSPerk*);
    POINTER_SKYRIMSE(TGetPerkRank, getPerkRank, 37698);

    return TiltedPhoques::ThisCall(getPerkRank, this, pPerk);
}

bool TP_MAKE_THISCALL(HookRemoveSpell, Actor, MagicItem* apSpell)
{
    bool result = TiltedPhoques::ThisCall(RealRemoveSpell, apThis, apSpell);
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

Actor* TP_MAKE_THISCALL(HookCharacterConstructor, Actor)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    TiltedPhoques::ThisCall(RealCharacterConstructor, apThis);

    return apThis;
}

Actor* TP_MAKE_THISCALL(HookCharacterConstructor2, Actor, uint8_t aUnk)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    TiltedPhoques::ThisCall(RealCharacterConstructor2, apThis, aUnk);

    return apThis;
}

Actor* TP_MAKE_THISCALL(HookCharacterDestructor, Actor)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    auto pExtension = apThis->GetExtension();

    if (pExtension)
    {
        pExtension->~ActorExtension();
    }

    TiltedPhoques::ThisCall(RealCharacterDestructor, apThis);

    return apThis;
}

GamePtr<Actor> Actor::New() noexcept
{
    auto* const pActor = Memory::Allocate<Actor>();

    TiltedPhoques::ThisCall(RealCharacterConstructor, pActor);

    return {pActor};
}

void Actor::InterruptCast(bool abRefund) noexcept
{
    TP_THIS_FUNCTION(TInterruptCast, void, Actor, bool abRefund);

    POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 38757);

    TiltedPhoques::ThisCall(s_interruptCast, this, abRefund);
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

    auto& modSystem = World::Get().GetModSystem();

    auto* pNpc = Cast<TESNPC>(baseForm);
    if (pNpc)
    {
        auto& factions = pNpc->actorData.factions;

        for (auto i = 0u; i < factions.length; ++i)
        {
            Faction faction;

            modSystem.GetServerModId(factions[i].faction->formID, faction.Id);
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

            modSystem.GetServerModId(pChanges->entries[i].faction->formID, faction.Id);
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
    TP_THIS_FUNCTION(TSetActorValueModifiers, void, Actor, int32_t aModifier, uint32_t aId, float aValue, Actor* apSource);
    POINTER_SKYRIMSE(TSetActorValueModifiers, s_setActorValueModifiers, 38468);
    constexpr int32_t cDamageModifier = 2;
    TiltedPhoques::ThisCall(s_setActorValueModifiers.Get(), this, cDamageModifier, ActorValueInfo::kHealth, -aDamage, apAttacker);
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

    auto& modSystem = World::Get().GetModSystem();

    uint32_t mainId = magicItems[0] ? magicItems[0]->formID : 0;
    modSystem.GetServerModId(mainId, equipment.LeftHandSpell);

    uint32_t secondaryId = magicItems[1] ? magicItems[1]->formID : 0;
    modSystem.GetServerModId(secondaryId, equipment.RightHandSpell);

    uint32_t shoutId = equippedShout ? equippedShout->formID : 0;
    modSystem.GetServerModId(shoutId, equipment.Shout);

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
    TP_THIS_FUNCTION(TGetGoldAmount, int32_t, const Actor);
    POINTER_SKYRIMSE(TGetGoldAmount, s_getGoldAmount, 37527);
    return TiltedPhoques::ThisCall(s_getGoldAmount, this);
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
    auto& modSystem = World::Get().GetModSystem();

    if (acEquipment.LeftHandSpell)
    {
        uint32_t mainHandWeaponId = modSystem.GetGameId(acEquipment.LeftHandSpell);
        spdlog::debug("Setting left hand spell: {:X}", mainHandWeaponId);
        pEquipManager->EquipSpell(this, TESForm::GetById(mainHandWeaponId), 0);
    }

    if (acEquipment.RightHandSpell)
    {
        uint32_t secondaryHandWeaponId = modSystem.GetGameId(acEquipment.RightHandSpell);
        spdlog::debug("Setting right hand spell: {:X}", secondaryHandWeaponId);
        pEquipManager->EquipSpell(this, TESForm::GetById(secondaryHandWeaponId), 1);
    }

    if (acEquipment.Shout)
    {
        uint32_t shoutId = modSystem.GetGameId(acEquipment.Shout);
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

    auto& modSystem = World::Get().GetModSystem();

    for (auto& entry : acFactions.NpcFactions)
    {
        auto pForm = GetById(modSystem.GetGameId(entry.Id));
        auto pFaction = Cast<TESFaction>(pForm);
        if (pFaction)
        {
            SetFactionRank(pFaction, entry.Rank);
        }
    }

    for (auto& entry : acFactions.ExtraFactions)
    {
        auto pForm = GetById(modSystem.GetGameId(entry.Id));
        auto pFaction = Cast<TESFaction>(pForm);
        if (pFaction)
        {
            SetFactionRank(pFaction, entry.Rank);
        }
    }
}

void Actor::SetFactionRank(const TESFaction* apFaction, int8_t aRank) noexcept
{
    TP_THIS_FUNCTION(TSetFactionRankInternal, void, Actor, const TESFaction*, int8_t);

    POINTER_SKYRIMSE(TSetFactionRankInternal, s_setFactionRankInternal, 37677);

    TiltedPhoques::ThisCall(s_setFactionRankInternal, this, apFaction, aRank);
}

void Actor::SetPlayerTeammate(bool aSet) noexcept
{
    TP_THIS_FUNCTION(TSetPlayerTeammate, void, Actor, bool aSet, bool abCanDoFavor);
    POINTER_SKYRIMSE(TSetPlayerTeammate, setPlayerTeammate, 37717);
    return TiltedPhoques::ThisCall(setPlayerTeammate, this, aSet, true);
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

TP_THIS_FUNCTION(TInitiateMountPackage, bool, Actor, Actor* apMount);
static TInitiateMountPackage* RealInitiateMountPackage = nullptr;

bool Actor::InitiateMountPackage(Actor* apMount) noexcept
{
    return TiltedPhoques::ThisCall(RealInitiateMountPackage, this, apMount);
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
    TP_THIS_FUNCTION(TStartDeferredKill, void, Actor, bool aScripted);
    POINTER_SKYRIMSE(TStartDeferredKill, s_startDeferredKill, 38616);
    TiltedPhoques::ThisCall(s_startDeferredKill.Get(), this, true);
}

void Actor::EndDeferredKill() noexcept
{
    // Actor::EndDeferredKill (38618); a kill it held back is queued (task queue), after any kill made now.
    TP_THIS_FUNCTION(TEndDeferredKill, void, Actor, bool aScripted);
    POINTER_SKYRIMSE(TEndDeferredKill, s_endDeferredKill, 38618);
    TiltedPhoques::ThisCall(s_endDeferredKill.Get(), this, true);
}

bool Actor::IsInKillMove() const noexcept
{
    // Actor::IsInKillMove (38615): either half of a kill move.
    TP_THIS_FUNCTION(TIsInKillMove, bool, const Actor);
    POINTER_SKYRIMSE(TIsInKillMove, s_isInKillMove, 38615);
    return TiltedPhoques::ThisCall(s_isInKillMove.Get(), this);
}

bool Actor::IsHeldByKillMove() const noexcept
{
    // Character 38619 (in a deferred kill, as KillActorHandler tests it): a kill move holds its victim so until KillActor.
    TP_THIS_FUNCTION(TIsInDeferredKill, bool, const Actor);
    POINTER_SKYRIMSE(TIsInDeferredKill, s_isInDeferredKill, 38619);
    return IsInKillMove() && TiltedPhoques::ThisCall(s_isInDeferredKill.Get(), this);
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
    const bool cRagdoll = acAction.EventName == "RagdollInstant";
    const ActionEvent cPrevious = pExtension->LatestAnimation;
    pExtension->LatestAnimation.ActionId = 0;
    EndDeferredKill();
    g_forceAnimation = true;
    KillImpl(nullptr, 100.f, true, cRagdoll);
    g_forceAnimation = false;
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

char TP_MAKE_THISCALL(HookSetPosition, Actor, NiPoint3& aPosition)
{
    const auto pExtension = apThis ? apThis->GetExtension() : nullptr;
    const auto bIsRemote = pExtension && pExtension->IsRemote();

    if (bIsRemote && !ScopedReferencesOverride::IsOverriden())
        return 1;

    // Don't interfere with non actor references, or the player, or if we are calling our self
    if (apThis->formType != Actor::Type || apThis == PlayerCharacter::Get() || ScopedReferencesOverride::IsOverriden())
        return TiltedPhoques::ThisCall(RealSetPosition, apThis, aPosition);

    ScopedReferencesOverride recursionGuard;

    // It just works TM
    apThis->SetPosition(aPosition, false);

    return 1;
}

TP_THIS_FUNCTION(TForceState, void, Actor, const NiPoint3&, float, float, TESObjectCELL*, TESWorldSpace*, bool);
static TForceState* RealForceState = nullptr;

void TP_MAKE_THISCALL(HookForceState, Actor, const NiPoint3& acPosition, float aX, float aZ, TESObjectCELL* apCell, TESWorldSpace* apWorldSpace, bool aUnkBool)
{
    /*const auto pNpc = Cast<TESNPC>(apThis->baseForm);
    if (pNpc)
    {
        spdlog::info("For TESNPC: {}, spawn at {} {} {}", pNpc->fullName.value, apPosition->m_x, apPosition->m_y,
                     apPosition->m_z);
    }*/

    // if (apThis != PlayerCharacter::Get())
    //     return;

    return TiltedPhoques::ThisCall(RealForceState, apThis, acPosition, aX, aZ, apCell, apWorldSpace, aUnkBool);
}

TP_THIS_FUNCTION(TDamageActor, bool, Actor, float aDamage, Actor* apHitter, bool aKillMove);
static TDamageActor* RealDamageActor = nullptr;

// TODO: this is flawed, since it does not account for invulnerable actors
// Set while an active effect applies its value (HookApplyActorEffect): damage then comes from an
// effect this game owns, never from a replayed melee swing.
static thread_local bool s_applyingEffect = false;

bool TP_MAKE_THISCALL(HookDamageActor, Actor, float aDamage, Actor* apHitter, bool aKillMove)
{
    if (apHitter)
        World::Get().GetRunner().Trigger(HitEvent(apHitter->formID, apThis->formID));

    float realDamage = GameplayFormulas::CalculateRealDamage(apThis, aDamage, aKillMove);

    float currentHealth = apThis->GetActorValue(ActorValueInfo::kHealth);
    bool wouldKill = (currentHealth - realDamage) <= 0.f;

    const auto* pExHittee = apThis->GetExtension();
    if (pExHittee->IsLocalPlayer())
    {
        if (!World::Get().GetServerSettings().PvpEnabled)
        {
            if (apHitter && apHitter->GetExtension()->IsRemotePlayer())
                return false;
        }

        World::Get().GetRunner().Trigger(HealthChangeEvent(apThis->formID, -realDamage));
        return TiltedPhoques::ThisCall(RealDamageActor, apThis, aDamage, apHitter, aKillMove);
    }
    else if (pExHittee->IsRemotePlayer())
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
            if (pExHittee->IsRemote())
                return wouldKill;
            return TiltedPhoques::ThisCall(RealDamageActor, apThis, aDamage, apHitter, aKillMove);
        }
        if (pExHitter->IsRemotePlayer())
        {
            // Our actor, damaged by an effect we applied at that player's request (see HookAddTarget):
            // apply it with them as the attacker, so the game handles aggro and kill credit.
            if (s_applyingEffect && pExHittee->IsLocal())
            {
                World::Get().GetRunner().Trigger(HealthChangeEvent(apThis->formID, -realDamage));
                return TiltedPhoques::ThisCall(RealDamageActor, apThis, aDamage, apHitter, aKillMove);
            }
            // Otherwise their replayed swing hit our actor; their game already sent the request.
            return wouldKill;
        }
    }

    if (pExHittee->IsLocal())
    {
        World::Get().GetRunner().Trigger(HealthChangeEvent(apThis->formID, -realDamage));
        return TiltedPhoques::ThisCall(RealDamageActor, apThis, aDamage, apHitter, aKillMove);
    }
    else
    {
        return wouldKill;
    }
}

TP_THIS_FUNCTION(TApplyActorEffect, void, ActiveEffect, Actor* apTarget, float aEffectValue, unsigned int unk1);
static TApplyActorEffect* RealApplyActorEffect = nullptr;

void TP_MAKE_THISCALL(HookApplyActorEffect, ActiveEffect, Actor* apTarget, float aEffectValue, unsigned int unk1)
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
                    return TiltedPhoques::ThisCall(RealApplyActorEffect, apThis, apTarget, aEffectValue, unk1);
                }
                return;
            }
        }
    }

    return TiltedPhoques::ThisCall(RealApplyActorEffect, apThis, apTarget, aEffectValue, unk1);
}

// The passive regeneration of health (24), magicka (25) and stamina (26): aDelta seconds' worth, restored
// into the damage modifier. The per-value updates (38451 health, 38452 stamina, 38453 magicka) call it every
// frame. Copies take these values from their owner, so they never regenerate. (Upstream hooked 37448,
// Actor::GetSubmergedLevel, here since its 2021 AE port; see docs/re/systems/actor-values.md.)
TP_THIS_FUNCTION(TRegenerateActorValue, void, Actor, int32_t aActorValue, float aDelta);
static TRegenerateActorValue* RealRegenerateActorValue = nullptr;

void TP_MAKE_THISCALL(HookRegenerateActorValue, Actor, int32_t aActorValue, float aDelta)
{
    if (apThis->GetExtension()->IsRemote())
        return;

    TiltedPhoques::ThisCall(RealRegenerateActorValue, apThis, aActorValue, aDelta);
}

void TP_MAKE_THISCALL(HookAddInventoryItem, Actor, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        auto& modSystem = World::Get().GetModSystem();

        Inventory::Entry item{};
        modSystem.GetServerModId(apItem->formID, item.BaseId);
        item.Count = aCount;

        if (apExtraData)
            apThis->GetItemFromExtraData(item, apExtraData);

        World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item)));
    }

    TiltedPhoques::ThisCall(RealAddInventoryItem, apThis, apItem, apExtraData, aCount, apOldOwner);
}

void* TP_MAKE_THISCALL(HookPickUpObject, Actor, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        auto& modSystem = World::Get().GetModSystem();

        Inventory::Entry item{};
        modSystem.GetServerModId(apObject->baseForm->formID, item.BaseId);
        item.Count = aCount;

        if (apObject->GetExtraDataList())
            apThis->GetItemFromExtraData(item, apObject->GetExtraDataList());

        // This is here so that objects that are picked up on both clients, aka non temps, are synced through activation sync.
        // The inventory change event should always be sent to the server, otherwise the server inventory won't be updated.
        bool shouldUpdateClients = apObject->IsTemporary() && !ScopedActivateOverride::IsOverriden();

        World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item), false, shouldUpdateClients));
    }

    return TiltedPhoques::ThisCall(RealPickUpObject, apThis, apObject, aCount, aUnk1, aUnk2);
}

void Actor::PickUpObject(TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2) noexcept
{
    TiltedPhoques::ThisCall(RealPickUpObject, this, apObject, aCount, aUnk1, aUnk2);
}

void* TP_MAKE_THISCALL(HookDropObject, Actor, void* apResult, TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation)
{
    auto& modSystem = World::Get().GetModSystem();

    Inventory::Entry item{};
    modSystem.GetServerModId(apObject->formID, item.BaseId);
    item.Count = -aCount;

    if (apExtraData)
        apThis->GetItemFromExtraData(item, apExtraData);

    World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item), true));

    ScopedInventoryOverride _;

    return TiltedPhoques::ThisCall(RealDropObject, apThis, apResult, apObject, apExtraData, aCount, apLocation, apRotation);
}

void Actor::DropOrPickUpObject(const Inventory::Entry& arEntry, NiPoint3* apLocation, NiPoint3* apRotation) noexcept
{
    ExtraDataList* pExtraData = GetExtraDataFromItem(arEntry);

    ModSystem& modSystem = World::Get().GetModSystem();

    uint32_t objectId = modSystem.GetGameId(arEntry.BaseId);
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
    TiltedPhoques::ThisCall(RealDropObject, this, &result, apObject, apExtraData, aCount, apLocation, apRotation);
}

TP_THIS_FUNCTION(TUpdateDetectionState, void, ActorKnowledge, void*);
static TUpdateDetectionState* RealUpdateDetectionState = nullptr;

void TP_MAKE_THISCALL(HookUpdateDetectionState, ActorKnowledge, void* apState)
{
    auto pOwner = TESObjectREFR::GetByHandle(apThis->hOwner);
    auto pTarget = TESObjectREFR::GetByHandle(apThis->hTarget);

    if (pOwner && pTarget)
    {
        auto pOwnerActor = Cast<Actor>(pOwner);
        auto pTargetActor = Cast<Actor>(pTarget);
        if (pOwnerActor && pTargetActor)
        {
            if (pOwnerActor->GetExtension()->IsRemotePlayer() && pTargetActor->GetExtension()->IsLocalPlayer())
            {
                spdlog::debug("Cancelling detection from remote player to local player, owner: {:X}, target: {:X}", pOwner->formID, pTarget->formID);
                return;
            }
        }
    }

    return TiltedPhoques::ThisCall(RealUpdateDetectionState, apThis, apState);
}

struct DialogueItem;

// TODO: This is an AIProcess function
// The engine passes a fifth argument (a byte it hands on to the response's processing); dropping it left
// the original reading an uninitialized stack slot.
TP_THIS_FUNCTION(TProcessResponse, uint64_t, void, DialogueItem* apVoice, Actor* apTalkingActor, Actor* apTalkedToActor, bool aUnk);
static TProcessResponse* RealProcessResponse = nullptr;

uint64_t TP_MAKE_THISCALL(HookProcessResponse, void, DialogueItem* apVoice, Actor* apTalkingActor, Actor* apTalkedToActor, bool aUnk)
{
    if (apTalkingActor)
    {
        if (apTalkingActor->GetExtension()->IsRemotePlayer())
            return 0;
    }
    return TiltedPhoques::ThisCall(RealProcessResponse, apThis, apVoice, apTalkingActor, apTalkedToActor, aUnk);
}

bool TP_MAKE_THISCALL(HookInitiateMountPackage, Actor, Actor* apMount)
{
    if (apMount && apThis->GetExtension()->IsLocal())
        World::Get().GetRunner().Trigger(MountEvent(apThis->formID, apMount->formID));

    return TiltedPhoques::ThisCall(RealInitiateMountPackage, apThis, apMount);
}

TP_THIS_FUNCTION(TUnequipObject, void, Actor, void* apUnk1, TESBoundObject* apObject, int32_t aUnk2, void* apUnk3);
static TUnequipObject* RealUnequipObject = nullptr;

void TP_MAKE_THISCALL(HookUnequipObject, Actor, void* apUnk1, TESBoundObject* apObject, int32_t aUnk2, void* apUnk3)
{
    TiltedPhoques::ThisCall(RealUnequipObject, apThis, apUnk1, apObject, aUnk2, apUnk3);
}

TP_THIS_FUNCTION(TSpeakSoundFunction, bool, Actor, const char* apName, uint32_t* a3, uint32_t a4, uint32_t a5, uint32_t a6, uint64_t a7, uint64_t a8, uint64_t a9, bool a10, uint64_t a11, bool a12, bool a13, bool a14);
static TSpeakSoundFunction* RealSpeakSoundFunction = nullptr;

bool TP_MAKE_THISCALL(HookSpeakSoundFunction, Actor, const char* apName, uint32_t* a3, uint32_t a4, uint32_t a5, uint32_t a6, uint64_t a7, uint64_t a8, uint64_t a9, bool a10, uint64_t a11, bool a12, bool a13, bool a14)
{
    spdlog::debug("a3: {:X}, a4: {}, a5: {}, a6: {}, a7: {}, a8: {:X}, a9: {:X}, a10: {}, a11: {:X}, a12: {}, a13: {}, a14: {}", (uint64_t)a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);

    if (apThis->GetExtension()->IsLocal())
        World::Get().GetRunner().Trigger(DialogueEvent(apThis->formID, apName));

    return TiltedPhoques::ThisCall(RealSpeakSoundFunction, apThis, apName, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
}

void Actor::SpeakSound(const char* pFile)
{
    uint32_t handle[3]{};
    handle[0] = -1;
    TiltedPhoques::ThisCall(RealSpeakSoundFunction, this, pFile, handle, 0, 0x32, 0, 0, 0, 0, 0, 0, 0, 1, 1);
}

// Actor::ApplyMovement (37356): the sync positions copies.
void TP_MAKE_THISCALL(HookApplyMovement, Actor, float aDelta)
{
    if (apThis->GetExtension()->IsRemote())
        return;

    TiltedPhoques::ThisCall(RealApplyMovement, apThis, aDelta);
}

// Character::UpdateAI (37347). Copies skip it but run its body upkeep below
// (docs/re/systems/actor-ai-and-copies.md).
TP_THIS_FUNCTION(TUpdateAI, void, Actor, float aDelta);
static TUpdateAI* RealUpdateAI = nullptr;
// Character::UpdateCellAttachment (37352): loads/unloads 3D as the actor's cell attaches.
TP_THIS_FUNCTION(TUpdateCellAttachment, void, Actor);
static TUpdateCellAttachment* UpdateCellAttachment = nullptr;
// Character::UpdateVoiceTimer (37752): counts the voice line down and marks it ended.
TP_THIS_FUNCTION(TUpdateVoiceTimer, void, Actor, bool aForceEnd);
static TUpdateVoiceTimer* UpdateVoiceTimer = nullptr;
// Character::CalculateEssentialProtected (37227): kEssential/kProtected.
TP_THIS_FUNCTION(TCalculateEssentialProtected, void, Actor);
static TCalculateEssentialProtected* CalculateEssentialProtected = nullptr;
// AIProcess::GetLipData (39373), AIProcess::UpdateLipSyncGraph (39377): mouth movement of races
// without a FaceGen head (dragons).
using TGetLipData = void*(AIProcess*);
static TGetLipData* GetLipData = nullptr;
using TUpdateLipSyncGraph = void(AIProcess*, void* apLipData, Actor* apActor);
static TUpdateLipSyncGraph* UpdateLipSyncGraph = nullptr;

void TP_MAKE_THISCALL(HookUpdateAI, Actor, float aDelta)
{
    if (apThis->GetExtension()->IsRemote())
    {
        // UpdateAI's own early return.
        if (!(apThis->flags1 & Actor::kProcessMe) || apThis->IsDisabled())
            return;

        TiltedPhoques::ThisCall(UpdateVoiceTimer, apThis, false);
        apThis->UpdateSoundCallback(false);

        // Copies die with their owner, never from their own health (as skymp keeps its remote actors).
        if (!apThis->IsDead())
            apThis->StartDeferredKill();

        AIProcess* pProcess = apThis->currentProcess;
        if (pProcess && pProcess->processLevel == 0)
        {
            TiltedPhoques::ThisCall(CalculateEssentialProtected, apThis);
            if (void* pLipData = GetLipData(pProcess); pLipData && apThis->race && !(apThis->race->raceFlags & TESRace::kFaceGenHead))
                UpdateLipSyncGraph(pProcess, pLipData, apThis);
        }

        // UpdateAI does this last, below high process.
        if (apThis->baseForm && (!pProcess || pProcess->processLevel != 0))
            TiltedPhoques::ThisCall(UpdateCellAttachment, apThis);
        return;
    }

    TiltedPhoques::ThisCall(RealUpdateAI, apThis, aDelta);
}

TP_THIS_FUNCTION(TAddDeathItems, void, Actor);
static TAddDeathItems* RealAddDeathItems = nullptr;

void TP_MAKE_THISCALL(HookAddDeathItems, Actor)
{
    if (apThis->GetExtension()->IsRemote())
        return;

    TiltedPhoques::ThisCall(RealAddDeathItems, apThis);
}

TP_THIS_FUNCTION(TIsFleeing, bool, Actor);
static TIsFleeing* RealIsFleeing = nullptr;

bool TP_MAKE_THISCALL(HookIsFleeing, Actor)
{
    // TODO: Player or RemotePlayer? Can players be in fleeing mode in skyrim?
    // TODO: investigate why the flee flag is set at all on remote players sometimes.
    if (apThis->GetExtension()->IsPlayer())
        return false;
    
    return TiltedPhoques::ThisCall(RealIsFleeing, apThis);
}

TP_THIS_FUNCTION(TIsHostileToActor, bool, Actor, Actor* apOther);
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
                spdlog::error("HangWatchdog: actor {:X} reference lock held: writer thread {}, lock word {:#x}", pActor->formID, pLock[0], pLock[1]);
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
                *fmt::format_to_n(apBuffer, aSize - 1, "actor {:X} reference lock", pActor->formID).out = '\0';
                return true;
            }
    return false;
}

void RefreshHangLockWatch() noexcept
{
    static const bool s_diagnosticSet = [] {
        CrashHandler::SetHangDiagnostic(&LogHeldReferenceLocks);
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

bool TP_MAKE_THISCALL(HookIsHostileToActor, Actor, Actor* apOther)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return true;
    return TiltedPhoques::ThisCall(RealIsHostileToActor, apThis, apOther);
}

// The relationship every hostility and combat decision reads: 0 neutral, 1 enemy, 2 ally, 3 friend.
// GetFactionReaction (37666) returns the value cached in the actor's knowledge of the other, else
// computes it (37667); the cache is refreshed from 37667. An angry actor sees the remote player as
// an enemy in both.
constexpr int32_t cReactionEnemy = 1;

TP_THIS_FUNCTION(TGetFactionReaction, int32_t, Actor, Actor* apOther);
static TGetFactionReaction* RealGetFactionReaction = nullptr;

int32_t TP_MAKE_THISCALL(HookGetFactionReaction, Actor, Actor* apOther)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return cReactionEnemy;
    return TiltedPhoques::ThisCall(RealGetFactionReaction, apThis, apOther);
}

// The core "may this actor attack that one" check (CanAttackActor, IsHostileToActor and combat
// use it). It reads the relationship cached in the actor's knowledge of the other directly, which
// still says ally right after the actor got angry, so answer it here too.
TP_THIS_FUNCTION(TShouldAttack, bool, Actor, Actor* apOther, int32_t aAggression, uint8_t aFlags);
static TShouldAttack* RealShouldAttack = nullptr;

bool TP_MAKE_THISCALL(HookShouldAttack, Actor, Actor* apOther, int32_t aAggression, uint8_t aFlags)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return true;
    return TiltedPhoques::ThisCall(RealShouldAttack, apThis, apOther, aAggression, aFlags);
}

TP_THIS_FUNCTION(TComputeReaction, int32_t, Actor, Actor* apOther, bool aUnk);
static TComputeReaction* RealComputeReaction = nullptr;

int32_t TP_MAKE_THISCALL(HookComputeReaction, Actor, Actor* apOther, bool aUnk)
{
    if (IsAngryWithRemotePlayer(apThis, apOther))
        return cReactionEnemy;
    return TiltedPhoques::ThisCall(RealComputeReaction, apThis, apOther, aUnk);
}

static TiltedPhoques::Initializer s_actorHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TApplyMovement, s_applyMovement, 37356);
        POINTER_SKYRIMSE(TUpdateAI, s_updateAI, 37347);
        POINTER_SKYRIMSE(TSetPosition, s_setPosition, 19790);
        POINTER_SKYRIMSE(TRemoveSpell, s_removeSpell, 38717);
        POINTER_SKYRIMSE(TCharacterConstructor, s_characterCtor, 40245);
        POINTER_SKYRIMSE(TCharacterConstructor2, s_characterCtor2, 40246);
        POINTER_SKYRIMSE(TCharacterDestructor, s_characterDtor, 37175);
        POINTER_SKYRIMSE(TGetLocation, s_GetActorLocation, 19812);
        POINTER_SKYRIMSE(TForceState, s_ForceState, 37313);
        POINTER_SKYRIMSE(TDamageActor, s_damageActor, 37335);
        POINTER_SKYRIMSE(TApplyActorEffect, s_applyActorEffect, 35086);
        POINTER_SKYRIMSE(TRegenerateActorValue, s_regenerateActorValue, 38459);
        POINTER_SKYRIMSE(TAddInventoryItem, s_addInventoryItem, 37525);
        POINTER_SKYRIMSE(TPickUpObject, s_pickUpObject, 37521);
        POINTER_SKYRIMSE(TDropObject, s_dropObject, 40454);
        POINTER_SKYRIMSE(TUpdateDetectionState, s_updateDetectionState, 42704);
        POINTER_SKYRIMSE(TProcessResponse, s_processResponse, 39643);
        POINTER_SKYRIMSE(TInitiateMountPackage, s_initiateMountPackage, 37905);
        POINTER_SKYRIMSE(TUnequipObject, s_unequipObject, 37975);
        POINTER_SKYRIMSE(TSpeakSoundFunction, s_speakSoundFunction, 37542);
        POINTER_SKYRIMSE(TAddDeathItems, addDeathItems, 37198);
        POINTER_SKYRIMSE(TIsFleeing, isFleeing, 37577);

        RealApplyMovement = s_applyMovement.Get();
        RealUpdateAI = s_updateAI.Get();
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
        RealSetPosition = s_setPosition.Get();
        RealRemoveSpell = s_removeSpell.Get();
        FUNC_GetActorLocation = s_GetActorLocation.Get();
        RealCharacterConstructor = s_characterCtor.Get();
        RealCharacterConstructor2 = s_characterCtor2.Get();
        RealForceState = s_ForceState.Get();
        RealDamageActor = s_damageActor.Get();
        RealApplyActorEffect = s_applyActorEffect.Get();
        RealRegenerateActorValue = s_regenerateActorValue.Get();
        RealAddInventoryItem = s_addInventoryItem.Get();
        RealPickUpObject = s_pickUpObject.Get();
        RealDropObject = s_dropObject.Get();
        RealUpdateDetectionState = s_updateDetectionState.Get();
        RealProcessResponse = s_processResponse.Get();
        RealInitiateMountPackage = s_initiateMountPackage.Get();
        RealUnequipObject = s_unequipObject.Get();
        RealSpeakSoundFunction = s_speakSoundFunction.Get();
        RealAddDeathItems = addDeathItems.Get();
        RealIsFleeing = isFleeing.Get();

        TP_HOOK(&RealApplyMovement, HookApplyMovement);
        TP_HOOK(&RealUpdateAI, HookUpdateAI);
        TP_HOOK(&RealSetPosition, HookSetPosition);
        TP_HOOK(&RealRemoveSpell, HookRemoveSpell);
        TP_HOOK(&RealCharacterConstructor, HookCharacterConstructor);
        TP_HOOK(&RealCharacterConstructor2, HookCharacterConstructor2);
        TP_HOOK(&RealForceState, HookForceState);
        TP_HOOK(&RealDamageActor, HookDamageActor);
        TP_HOOK(&RealApplyActorEffect, HookApplyActorEffect);
        TP_HOOK(&RealRegenerateActorValue, HookRegenerateActorValue);

        POINTER_SKYRIMSE(TIsHostileToActor, s_isHostileToActor, 37537);
        RealIsHostileToActor = s_isHostileToActor.Get();
        TP_HOOK(&RealIsHostileToActor, HookIsHostileToActor);

        POINTER_SKYRIMSE(TGetFactionReaction, s_getFactionReaction, 37666);
        RealGetFactionReaction = s_getFactionReaction.Get();
        TP_HOOK(&RealGetFactionReaction, HookGetFactionReaction);

        POINTER_SKYRIMSE(TShouldAttack, s_shouldAttack, 37534);
        RealShouldAttack = s_shouldAttack.Get();
        TP_HOOK(&RealShouldAttack, HookShouldAttack);

        POINTER_SKYRIMSE(TComputeReaction, s_computeReaction, 37667);
        RealComputeReaction = s_computeReaction.Get();
        TP_HOOK(&RealComputeReaction, HookComputeReaction);
        TP_HOOK(&RealAddInventoryItem, HookAddInventoryItem);
        TP_HOOK(&RealPickUpObject, HookPickUpObject);
        TP_HOOK(&RealDropObject, HookDropObject);
        TP_HOOK(&RealUpdateDetectionState, HookUpdateDetectionState);
        TP_HOOK(&RealProcessResponse, HookProcessResponse);
        TP_HOOK(&RealInitiateMountPackage, HookInitiateMountPackage);
        TP_HOOK(&RealUnequipObject, HookUnequipObject);
        TP_HOOK(&RealSpeakSoundFunction, HookSpeakSoundFunction);
        TP_HOOK(&RealAddDeathItems, HookAddDeathItems);
        TP_HOOK(&RealIsFleeing, HookIsFleeing);
    });
