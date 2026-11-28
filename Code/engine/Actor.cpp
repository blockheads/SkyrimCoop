#include <CopyCombatGroups.h>
#include <Forms/BGSAction.h>
#include <Interface/ItemMenuWatch.h>
#include <animation/ActorMediator.h>
#include <Animation/TESActionData.h>
#include <animation/ReplayState.h>
#include <animation/ActorStateRules.h>
#include <animation/GraphReference.h>
#include <BSAnimationGraphManager.h>
#include <CrashHandler.h>
#include <HookSwitch.h>
#include <BSCore/LockOrder.h>
#include <core/EngineNames.h>
#include <References.h>
#include <characters/ActorPools.h>
#include <EquipManager.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <Misc/TaskQueueInterface.h>
#include <Havok/bhkCharacterController.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <movement/CopyStep.h>
#include <Combat/WorldHits.h>
#include <Misc/GameVM.h>
#include <DefaultObjectManager.h>
#include <Forms/TESNPC.h>
#include <Forms/TESFaction.h>
#include <Components/TESActorBaseData.h>
#include <ExtraData/ExtraFactionChanges.h>
#include <Memory.h>
#include <RefrInteraction.h>
#include <Forms/TESLevItem.h>
#include <Combat/CombatController.h>

#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <Papyrus/GuestScripts.h>
#include <items/InventoryChangeEvent.h>
#include <items/WornResetEvent.h>
#include <items/LooseItemEvents.h>
#include <mounts/MountEvent.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <dialogue/DialogueEvent.h>
#include <combat/HitEvent.h>
#include <Combat/HitOwnership.h>
#include <Combat/PartyAggro.h>
#include <magic/RemoveSpellEvent.h>

#include <TES.h>
#include <GuestNpcs.h>
#include <characters/NpcHeldEvent.h>
#include <core/World.h>
#include <ports/IActorBridge.h>
#include <world/ActivatePrompts.h>
#include <dialogue/SpeakerUpdate.h>
#include <Bridges/PapyrusService.h>
#include <death/DeathDrops.h>

#include <Forms/ActorValueInfo.h>
#include <Forms/TESRace.h>

#include <Effects/ValueModifierEffect.h>

#include <Misc/InventoryEntry.h>
#include <ExtraData/ExtraCount.h>
#include <Misc/ActorKnowledge.h>
#include <Crime.h>
#include <crime/DetectionRules.h>

#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraInteraction.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <core/ItemIds.h>
#include <Forms/TESObjectCELL.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_BHR_Master.h>

#include <items/Overrides.h>
#include <BSAnimationGraphManager.h>
#include <Havok/hkbStateMachine.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Forms/BGSOutfit.h>
#include <ScriptEquips.h>
#include <Forms/TESObjectARMO.h>


#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <Utils.h>
#include <VersionDb.h>
#include <core/FormIds.h>

using TRemoveSpell = bool(Actor*, MagicItem*);

using TCharacterConstructor = Actor*(Actor*);

using TCharacterConstructor2 = Actor*(Actor*, uint8_t aUnk);

using TGetLocation = TESForm*(TESForm*);

static TGetLocation* FUNC_GetActorLocation;

TCharacterConstructor* RealCharacterConstructor;

TCharacterConstructor2* RealCharacterConstructor2;

static TRemoveSpell* RealRemoveSpell = nullptr;

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

bool Actor::CapsuleCollides() const noexcept
{
    const auto* pProcess = currentProcess;
    const auto* pController = pProcess && pProcess->middleProcess ? pProcess->middleProcess->charController : nullptr;
    if (!pController)
        return true;
    uint32_t filter = 0;
    pController->GetCollisionFilterInfo(filter);
    return (filter & 0x7F) != bhkCharacterController::kCapsuleLayer || !(filter & bhkCharacterController::kFilterNoCollision);
}

bool Actor::SetCapsuleCollision(bool aCollides) noexcept
{
    auto* pProcess = currentProcess;
    auto* pController = pProcess && pProcess->middleProcess ? pProcess->middleProcess->charController : nullptr;
    hkpRigidBody* pBody = pController ? pController->GetRigidBody() : nullptr;
    if (!pBody || !pBody->userData)
        return false;
    uint32_t filter = 0;
    pController->GetCollisionFilterInfo(filter);
    if ((filter & 0x7F) != bhkCharacterController::kCapsuleLayer)
        return false;
    const uint32_t cWanted = aCollides ? filter & ~bhkCharacterController::kFilterNoCollision : filter | bhkCharacterController::kFilterNoCollision;
    if (cWanted != filter)
    {
        // bhkWorldObject::SetCollisionFilterInfo: its filter, re-filtered in the world.
        using TSetFilter = void(bhkWorldObject*, uint32_t);
        POINTER_SKYRIMSE(TSetFilter, s_setFilter, 37006);
        s_setFilter(pBody->userData, cWanted);
    }
    return true;
}

namespace
{
// Its first ragdoll body: the ragdoll's bodies are its 3D's (40063 and 39912 set them all at once), else null.
const hkpRigidBody* FirstBodyOf(const Actor& acActor) noexcept
{
    for (const bhkWorldObject* pBody : RagdollBodiesOf(acActor))
        if (pBody && pBody->referencedObject)
            return pBody->referencedObject;
    return nullptr;
}

uint8_t LayerOf(const hkpRigidBody* apBody) noexcept
{
    return apBody && apBody->world ? static_cast<uint8_t>(apBody->CollisionFilterInfo() & 0x7F) : 0;
}
} // namespace

uint8_t Actor::BodiesLayer() const noexcept
{
    return LayerOf(FirstBodyOf(*this));
}

bool Actor::SetBodiesLayer(uint8_t aLayer, const Actor* apGroupOf) noexcept
{
    NiNode* pRoot = GetNiNode();
    auto* pProcess = currentProcess;
    auto* pController = pProcess && pProcess->middleProcess ? pProcess->middleProcess->charController : nullptr;
    // 58291 sets each body by its bone's keyframe track; a dynamic one poses its bone (78910), in the world or not.
    const hkpRigidBody* pBody = nullptr;
    bool anyDynamic = false;
    for (const bhkWorldObject* pEach : RagdollBodiesOf(*this))
        if (pEach && pEach->referencedObject)
        {
            pBody = pBody ? pBody : pEach->referencedObject;
            anyDynamic |= pEach->referencedObject->IsDynamic();
        }
    if (!pRoot || !pController || !parentCell || !pBody)
        return false;
    // Its own capsule's group, or its mount's: one group never collides, as SeatRider (40358) sets a rider's.
    const auto* pGroupProcess = apGroupOf ? apGroupOf->currentProcess : nullptr;
    auto* pGroupController = pGroupProcess && pGroupProcess->middleProcess ? pGroupProcess->middleProcess->charController : nullptr;
    uint32_t groupFilter = 0;
    (pGroupController ? pGroupController : pController)->GetCollisionFilterInfo(groupFilter);
    const uint32_t cGroup = groupFilter >> 16;
    if (LayerOf(pBody) == aLayer && (aLayer ? pBody->CollisionFilterInfo() >> 16 == cGroup : !anyDynamic))
        return false;
    const bool cInWorld = pBody->world != nullptr;

    using TAddToWorld = void(void*, NiAVObject*, void* apWorld, int32_t);
    using TSetMotionType = void(void*, NiAVObject*, uint32_t, bool);
    using TSetLayerAndGroup = void(void*, NiAVObject*, uint32_t, uint32_t);
    POINTER_SKYRIMSE(TAddToWorld, s_addToWorld, 36922);
    POINTER_SKYRIMSE(TSetMotionType, s_setMotionType, 36925);
    POINTER_SKYRIMSE(TSetLayerAndGroup, s_setLayerAndGroup, 36929);
    TaskQueueInterface* pQueue = TaskQueueInterface::Get();
    if (!pQueue)
        return false;
    // Out, as EndKnockState (39897) ends a living actor's ragdoll: keyframed too, so no bone stays on a still body.
    s_setMotionType.Get()(pQueue, pRoot, hkpRigidBody::kKeyframed, false);
    if (!aLayer)
    {
        pQueue->QueueRemoveFromWorld(pRoot);
        return true;
    }
    // As EnterFurnitureCollision (40063): the layer with its capsule's group (a rider's: its mount's), in its cell's
    // world (39912 changes only the layer of bodies in the world already).
    s_setLayerAndGroup.Get()(pQueue, pRoot, aLayer, cGroup);
    if (!cInWorld)
        s_addToWorld.Get()(pQueue, pRoot, nullptr, 0);
    return true;
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

// The weaponDraw or weaponSheathe event's result with no action: RightHandWeaponDrawHandler (42824, its handler
// unused), 37169. Both set the weapon state through ActorState vfunc 21, which skips a pending opposite request.
void Actor::ShowWeaponDrawn(bool aDrawn) noexcept
{
    using TDrawn = bool(void*, Actor*);
    using TSheathed = void(Actor*);
    POINTER_SKYRIMSE(TDrawn, s_drawn, 42824);
    POINTER_SKYRIMSE(TSheathed, s_sheathed, 37169);

    if (actorState.GetWeaponState() == 1)
        actorState.SetWeaponStateFlags(0);
    else if (actorState.GetWeaponState() == 4)
        actorState.SetWeaponStateFlags(3);
    if (aDrawn)
        s_drawn(nullptr, this);
    else
        s_sheathed(this);
}

void Actor::SetEssentialEx(bool aSet) noexcept
{
    SetEssential(aSet);
    if (TESNPC* pBase = Cast<TESNPC>(baseForm))
        pBase->actorData.SetEssential(aSet);
}

void Actor::JoinPlayerFaction() noexcept
{
    if (auto* pPlayerFaction = Cast<TESFaction>(TESForm::GetById(0xDB1)))
        SetFactionRank(pPlayerFaction, 1);
}

void Actor::CastPermanentMagic(bool aWornItemEnchantments, bool aBaseSpells, bool aRaceSpells, bool aEveryActorAbility) noexcept
{
    using TCastPermanentMagic = void(Actor*, bool, bool, bool, bool);
    POINTER_SKYRIMSE(TCastPermanentMagic, s_castPermanentMagic, 38753);
    s_castPermanentMagic(this, aWornItemEnchantments, aBaseSpells, aRaceSpells, aEveryActorAbility);
}

void Actor::DoReset3D(bool aUpdateWeight) noexcept
{
    using TDoReset3D = void(Actor*, bool);
    POINTER_SKYRIMSE(TDoReset3D, s_doReset3D, 40255);
    s_doReset3D(this, aUpdateWeight);
}

void Actor::ClearPowerCooldowns() noexcept
{
    POINTER_SKYRIMSE(void(Actor*), s_clear, 38825);
    s_clear(this);
}

void Actor::SetPowerCooldown(SpellItem* apPower, float aSeconds) noexcept
{
    POINTER_SKYRIMSE(void(Actor*, SpellItem*, float), s_set, 38738);
    s_set(this, apPower, aSeconds);
}

GameList<ActiveEffect>* Actor::GetActiveEffects() noexcept
{
    POINTER_SKYRIMSE(GameList<ActiveEffect>*(MagicTarget*), s_effects, 38795);
    return s_effects(&magicTarget);
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
        World::Runner().Trigger(removalEvent);
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

float Actor::GetActorValue(uint32_t aId) const noexcept
{
    return EngineVirtual(actorValueOwner).GetValue(aId);
}

float Actor::GetActorPermanentValue(uint32_t aId) const noexcept
{
    return EngineVirtual(actorValueOwner).GetPermanentValue(aId);
}

float Actor::GetActorValueModifier(ActorValueOwner::ForceMode aMode, uint32_t aId) const noexcept
{
    // CommonLibSSE-NG Actor::GetActorValueModifier (38469).
    POINTER_SKYRIMSE(float(const Actor*, uint32_t, uint32_t), s_modifier, 38469);
    return s_modifier(this, static_cast<uint32_t>(aMode), aId);
}

float Actor::GetActorTemporaryValue(uint32_t aId) const noexcept
{
    return GetActorValueModifier(ActorValueOwner::ForceMode::TEMPORARY, aId);
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
    ActorPools::Restore(acActorValues, [this](int32_t aId) { return GetActorValue(aId); },
        [this](int32_t aId) { return GetActorTemporaryValue(aId); },
        [this](int32_t aMode, int32_t aId, float aTarget) {
            ForceActorValue(static_cast<ActorValueOwner::ForceMode>(aMode), aId, aTarget);
        });
    for (const auto& [cId, cValue] : acActorValues.ActorValuesList)
        if (!ActorPools::IsPool(cId))
            SetActorValue(cId, cValue);
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

void Actor::SeatOnMount(Actor* apMount) noexcept
{
    const RefrInteraction* pLink = GetOwnInteraction();
    const bool cLinked = pLink && TESObjectREFR::GetByHandle(pLink->actorHandle) == this && TESObjectREFR::GetByHandle(pLink->targetHandle) == apMount;
    if (!cLinked && !PutOnMount(apMount))
        return;
    actorState.SetSitSleepStateFlags(3);
    OnInteractionSeated();
}

void Actor::OnInteractionSeated() noexcept
{
    using TSeated = void(Actor*);
    POINTER_SKYRIMSE(TSeated, s_onSeated, 41533);
    s_onSeated(this);
}

bool Actor::SitOnMountQuick(Actor* apMount) noexcept
{
    return PutOnMount(apMount) && PutActorOnMountQuick();
}

TESObjectREFR* Actor::GetSeatFurniture() const noexcept
{
    using TGetOccupiedFurniture = uint32_t*(const Actor*, uint32_t* apHandle);
    POINTER_SKYRIMSE(TGetOccupiedFurniture, s_getOccupiedFurniture, 38708);
    // The engine leaves the handle set after the actor stands (actor-ai-and-copies.md).
    if (!actorState.GetSitSleepState())
        return nullptr;
    uint32_t handle = 0;
    s_getOccupiedFurniture(this, &handle);
    TESObjectREFR* pFurniture = TESObjectREFR::GetByHandle(handle);
    return pFurniture && pFurniture->formType != FormType::Character ? pFurniture : nullptr;
}

void Actor::LinkFurnitureGraphs(TESObjectREFR* apFurniture) noexcept
{
    using TAddDependent = bool(BSAnimationGraphManager*, BSAnimationGraphManager** appDependent, bool aShare);
    using TRemoveDependent = bool(BSAnimationGraphManager*, BSAnimationGraphManager** appDependent);
    POINTER_SKYRIMSE(TAddDependent, s_addDependent, 63364);
    POINTER_SKYRIMSE(TRemoveDependent, s_removeDependent, 63366);
    const auto cGraphsOf = [](const TESObjectREFR* apObject) {
        BSAnimationGraphManager* pManager = nullptr;
        if (apObject)
            EngineVirtual(apObject->animationGraphHolder).GetBSAnimationGraph(&pManager);
        return GraphReference<BSAnimationGraphManager>(pManager);
    };
    ActorExtension& link = *GetExtension();
    const uint32_t cWanted = apFurniture ? apFurniture->formID : 0;
    const auto cOwn = cGraphsOf(this);
    const auto cFurniture = cGraphsOf(apFurniture);
    if (link.LinkedFurniture == cWanted && link.LinkedOwnGraphs == cOwn.get() && link.LinkedFurnitureGraphs == cFurniture.get())
        return;
    // The old link, on the same manager of its own (a new one starts with none); the list holds the furniture's.
    if (link.LinkedFurnitureGraphs && cOwn && link.LinkedOwnGraphs == cOwn.get())
    {
        auto* pOld = const_cast<BSAnimationGraphManager*>(static_cast<const BSAnimationGraphManager*>(link.LinkedFurnitureGraphs));
        s_removeDependent(cOwn.get(), &pOld);
    }
    link.LinkedFurniture = 0;
    link.LinkedOwnGraphs = link.LinkedFurnitureGraphs = nullptr;
    // Either graph missing (a 3D still loading): linked when it loads (AiStateService::RelinkSeats).
    if (!cOwn || !cFurniture)
        return;
    BSAnimationGraphManager* pNew = cFurniture.get();
    s_addDependent(cOwn.get(), &pNew, true);
    link.LinkedFurniture = cWanted;
    link.LinkedOwnGraphs = cOwn.get();
    link.LinkedFurnitureGraphs = cFurniture.get();
}

RefrInteraction* Actor::GetOwnInteraction() const noexcept
{
    const auto* pExtra = static_cast<const ExtraInteraction*>(extraData.GetByType(ExtraDataType::Interaction));
    return pExtra ? pExtra->interaction : nullptr;
}

TESObjectREFR* Actor::GetRiddenMount() const noexcept
{
    const RefrInteraction* pLink = GetOwnInteraction();
    if (!pLink || TESObjectREFR::GetByHandle(pLink->actorHandle) != this)
        return nullptr;
    return TESObjectREFR::GetByHandle(pLink->targetHandle);
}

void Actor::HoldRider(Actor* apRider, bool aReplace) noexcept
{
    RefrInteraction* pLink = apRider ? apRider->GetOwnInteraction() : nullptr;
    if (aReplace || !pLink)
    {
        RefrInteraction* pNone = nullptr;
        extraData.SetInteraction(&pNone);
    }
    if (pLink)
        extraData.SetInteraction(&pLink);
}

void Actor::EvaluatePackageNow(bool aResetAI) noexcept
{
    using TEvaluate = void(Actor*, bool aImmediate, bool aResetAI);
    POINTER_SKYRIMSE(TEvaluate, s_evaluate, 37401);
    s_evaluate(this, true, aResetAI);
}

void Actor::EndMountPair() noexcept
{
    BSFixedString end("PairEnd");
    SendAnimationEvent(&end);
}

bool Actor::StopRidingQuick() noexcept
{
    using TStopRidingQuick = bool(Actor*, uint32_t aMode);
    POINTER_SKYRIMSE(TStopRidingQuick, s_stopRidingQuick, 38705);
    return s_stopRidingQuick(this, 2);
}

void Actor::EndRideNow() noexcept
{
    using TSetSitSleepState = void(AIProcess*, Actor*, uint32_t aState, uint32_t* apFurniture, int32_t aMarker);
    using TFinishDismount = void(Actor*);
    POINTER_SKYRIMSE(TSetSitSleepState, s_setSitSleepState, 39912);
    POINTER_SKYRIMSE(TFinishDismount, s_finishDismount, 38696);
    if (currentProcess)
    {
        uint32_t noFurniture = 0; // g_invalidRefHandle
        s_setSitSleepState(currentProcess, this, 0, &noFurniture, -1);
    }
    s_finishDismount(this);
}

void Actor::FinishDismount() noexcept
{
    using TFinishDismount = void(Actor*);
    POINTER_SKYRIMSE(TFinishDismount, s_finishDismount, 38696);
    s_finishDismount(this);
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

bool Actor::LiesInRagdoll() const noexcept
{
    return (IsDead() || actorState.GetKnockState() != 0) && IsInRagdollState();
}

bool Actor::IsInRagdollState() const noexcept
{
    using TIsInRagdollState = bool(const Actor*);
    POINTER_SKYRIMSE(TIsInRagdollState, s_isInRagdollState, 37491);
    return s_isInRagdollState.Get()(this);
}

bool Actor::IsInDeferredKill() const noexcept
{
    using TIsInDeferredKill = bool(const Actor*);
    POINTER_SKYRIMSE(TIsInDeferredKill, s_isInDeferredKill, 38619);
    return s_isInDeferredKill.Get()(this);
}

TESFaction* Actor::GetCrimeFaction() const noexcept
{
    using TGetCrimeFaction = TESFaction*(const Actor*);
    POINTER_SKYRIMSE(TGetCrimeFaction, s_getCrimeFaction, 37380);
    return s_getCrimeFaction.Get()(this);
}

int32_t Actor::GetFactionReaction(Actor* apOther) const noexcept
{
    using TGetFactionReaction = int32_t(const Actor*, Actor*);
    POINTER_SKYRIMSE(TGetFactionReaction, s_getFactionReaction, 37666);
    return s_getFactionReaction.Get()(this, apOther);
}

bool Actor::IsHostileToActor(Actor* apOther) noexcept
{
    using TIsHostileToActor = bool(Actor*, Actor*);
    POINTER_SKYRIMSE(TIsHostileToActor, s_isHostileToActor, 37537);
    return s_isHostileToActor.Get()(this, apOther);
}

bool Actor::CanBeKilledBy(Actor* apKiller) noexcept
{
    using TCanBeKilledBy = bool(Actor*, Actor*);
    POINTER_SKYRIMSE(TCanBeKilledBy, s_canBeKilledBy, 37229);
    return s_canBeKilledBy.Get()(this, apKiller);
}

bool Actor::EngagingInCombat() noexcept
{
    using TEngagingInCombat = bool(Actor*);
    POINTER_SKYRIMSE(TEngagingInCombat, s_engagingInCombat, 38563);
    return s_engagingInCombat.Get()(this);
}

bool Actor::HasLineOfSight(TESObjectREFR* apTarget, bool& aUnknown) noexcept
{
    using THasLineOfSight = bool(Actor*, TESObjectREFR*, bool&);
    POINTER_SKYRIMSE(THasLineOfSight, s_hasLineOfSight, 53829);
    return s_hasLineOfSight.Get()(this, apTarget, aUnknown);
}

bool Actor::IsDragon() const noexcept
{
    const ActorExtension* pExtension = const_cast<Actor*>(this)->GetExtension();
    return pExtension->GraphDescriptorHash == AnimationGraphDescriptor_BHR_Master::m_key;
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

bool Actor::IsHeldOnGuest() noexcept
{
    const uint32_t cSession = GuestNpcs::Session();
    if (!cSession)
        return false;
    const ActorExtension* pExtension = GetExtension();
    return GuestNpcs::Held(cSession, this == PlayerCharacter::Get(), pExtension->IsCopy(), pExtension->ClaimedIn.load());
}

bool Actor::RefuseHeld3D() noexcept
{
    ActorExtension* pExtension = GetExtension();
    if (IsHeldOnGuest())
    {
        // Marked before the second look: SetActorCopy queues it once it reads the mark, or this sees the copy.
        pExtension->Refused3D.store(true);
        if (IsHeldOnGuest())
            return true;
    }
    pExtension->Refused3D.store(false);
    return false;
}

void Actor::FinishLoad3D() noexcept
{
    const bool cRefused = GetExtension()->Refused3D.load();
    if (!cRefused && !(GetNiNode() && IsHeldOnGuest()))
        return;
    const uint32_t cFormId = formID;
    if (cRefused)
        World::Runner().Trigger(NpcHeldEvent{cFormId});
    World::Runner().Queue([cFormId] {
        if (auto* pActor = Cast<Actor>(TESForm::GetById(cFormId)))
            pActor->ApplyHeld3D();
    });
}

void Actor::ApplyHeld3D() noexcept
{
    // Held with 3D (shown before holding started, or a load begun then): reloaded, so refused.
    if (IsHeldOnGuest())
    {
        if (GetNiNode() && !IsDisabled())
            Reload3D();
        return;
    }
    if (!GetExtension()->Refused3D.load())
        return;
    // As Enable (19800): its cell loaded (TES::IsCellLoaded 13308), no 3D, then QueueReference (13057) at the cell's
    // priority (TES::GetCellLoadPriority 13321). A job still out only takes the priority: its Finish calls this again.
    using TIsCellLoaded = bool(TES*, TESObjectCELL*, bool aAttachedOnly);
    using TGetCellLoadPriority = uint32_t(TES*, TESObjectCELL*, bool aFromCamera);
    using TQueueReference = void(void* apQueuedHandles, TESObjectREFR*, uint32_t aPriority, bool aNow);
    POINTER_SKYRIMSE(TIsCellLoaded, s_isCellLoaded, 13308);
    POINTER_SKYRIMSE(TGetCellLoadPriority, s_getCellLoadPriority, 13321);
    POINTER_SKYRIMSE(TQueueReference, s_queueReference, 13057);
    POINTER_SKYRIMSE(void*, s_queuedHandles, 400332);
    TES* pTes = TES::Get();
    if (!pTes || !parentCell || IsDeleted() || GetNiNode() || !s_isCellLoaded(pTes, parentCell, false))
        return;
    s_queueReference(*s_queuedHandles.Get(), this, s_getCellLoadPriority(pTes, parentCell, false), false);
}

bool Actor::IsInKillMove() const noexcept
{
    // Actor::IsInKillMove (38615): either half of a kill move.
    using TIsInKillMove = bool(const Actor*);
    POINTER_SKYRIMSE(TIsInKillMove, s_isInKillMove, 38615);
    return (s_isInKillMove.Get())(this);
}

void Actor::EndPairs() noexcept
{
    // BGSSynchronizedAnimationManager::EndPairsOf 32809; its instances send the halves PairEnd, then PairedStop.
    using TEndPairsOf = void(void*, const uint32_t*);
    POINTER_SKYRIMSE(TEndPairsOf, s_endPairsOf, 32809);
    POINTER_SKYRIMSE(void*, s_manager, 401100);
    const uint32_t cHandle = GetHandle().handle.iBits;
    (s_endPairsOf.Get())(*s_manager.Get(), &cHandle);
    StopPairClip();
}

bool Actor::InPair() const noexcept
{
    using TIsInInstance = bool(void*, const uint32_t*);
    POINTER_SKYRIMSE(TIsInInstance, s_isInInstance, 32808);
    POINTER_SKYRIMSE(void*, s_manager, 401100);
    const uint32_t cHandle = GetHandle().handle.iBits;
    return *s_manager.Get() && (s_isInInstance.Get())(*s_manager.Get(), &cHandle);
}

bool Actor::InPairClip() noexcept
{
    // The graph's bIsSynced, on through a pair's clip.
    BSFixedString name("bIsSynced");
    bool value = false;
    EngineVirtual(animationGraphHolder).GetVariableBool(&name, &value);
    return value;
}

void Actor::StopPairClip() noexcept
{
    // An attacker's KillMoveState leaves only on pairedStop, sent by the instance only if PairEnd went unanswered.
    BSFixedString stop("pairedStop");
    SendAnimationEvent(&stop);
}

void Actor::EnterKillMove(Actor* apKiller) noexcept
{
    using TEnterKillMove = int64_t(Actor*, Actor*);
    POINTER_SKYRIMSE(TEnterKillMove, s_enterKillMove, 38613);
    (s_enterKillMove.Get())(this, apKiller);
}

void Actor::EndKillMove() noexcept
{
    // Character 38614: clears the kill move flag, and kills an actor with a recorded killer.
    using TEndKillMove = void(Actor*);
    POINTER_SKYRIMSE(TEndKillMove, s_endKillMove, 38614);
    (s_endKillMove.Get())(this);
}

bool Actor::IsHeldByKillMove() const noexcept
{
    // In a deferred kill, as KillActorHandler tests it: a kill move holds its victim so until KillActor.
    return IsInKillMove() && IsInDeferredKill();
}

void Actor::Kill() noexcept
{
    // Never this game's player; a player's copy dies as its owner did.
    ActorExtension* pExtension = GetExtension();
    if (pExtension->IsLocalPlayer())
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

void Actor::HoldKill(bool aHold) noexcept
{
    GetExtension()->KillHeld.store(aHold, std::memory_order_relaxed);
    if (aHold)
    {
        StartDeferredKill();
        return;
    }
    // A death is its owner's word: a kill counted meanwhile is not queued for the player (40036).
    if (auto* pMiddle = currentProcess ? currentProcess->middleProcess : nullptr; pMiddle && pMiddle->deferredKill > 1)
        pMiddle->deferredKill = 1;
    EndDeferredKill();
}

bool Actor::KillCounted() const noexcept
{
    const auto* pMiddle = currentProcess ? currentProcess->middleProcess : nullptr;
    return pMiddle && pMiddle->deferredKill > 1;
}

bool Actor::KillQueued() const noexcept
{
    const auto* pMiddle = currentProcess ? currentProcess->middleProcess : nullptr;
    return pMiddle && pMiddle->killQueued;
}

void Actor::KillBy(Actor* apKiller) noexcept
{
    if (GetExtension()->IsLocalPlayer())
        return;
    GetExtension()->PoseHeldUntil.store(0, std::memory_order_relaxed);
    HoldKill(false);
    KillImpl(apKiller, 100.f, true, false);
}

bool Actor::PerformPlainly(BGSAction* apAction) noexcept
{
    if (!apAction)
        return false;
    // Through 38949's own entry, as 42350's Func5 (41557) goes: our hook records and sends it, and its swing ends the
    // yield's blow guard (ReplayState::StartsAttack).
    using TPerformAction = uint8_t(ActorMediator*, TESActionData*);
    POINTER_SKYRIMSE(TPerformAction, s_performAction, 38949);
    TESActionData actionData(2, this, apAction, nullptr);
    const bool cTaken = s_performAction.Get()(ActorMediator::Get(), &actionData) != 0;
    spdlog::debug("{:X} performs {:X} with no target: '{}' {}", formID, apAction->formID, actionData.eventName.AsAscii(), cTaken ? "taken" : "refused");
    return cTaken;
}

bool Actor::KillWithDeathAction(const ActionEvent& acAction) noexcept
{
    ActorExtension* pExtension = GetExtension();
    if (pExtension->IsLocalPlayer())
        return false;

    // KillImpl (37896) performs the death animation, or RagdollInstant when told to ragdoll at once; a
    // copy's own actions are refused unless forced.
    const bool cRagdoll = SameName(acAction.EventName, "RagdollInstant");
    const auto cAttempt = pExtension->LatestAnimation.BeginAttempt();
    pExtension->PoseHeldUntil.store(0, std::memory_order_relaxed);
    EndDeferredKill();
    g_forceAnimation = true;
    KillImpl(nullptr, 100.f, true, cRagdoll);
    g_forceAnimation = false;
    World::Runner().Trigger(DeathActionReplayedEvent{formID, acAction.Tick});
    return pExtension->LatestAnimation.FinishAttempt(acAction.ActionId, cAttempt);
}

void Actor::KillBesideGraph(const ActionEvent& acAction) noexcept
{
    ActorExtension* pExtension = GetExtension();
    if (pExtension->IsLocalPlayer())
        return;
    pExtension->PoseHeldUntil.store(0, std::memory_order_relaxed);
    EndDeferredKill();
    KillImpl(nullptr, 100.f, true, SameName(acAction.EventName, "RagdollInstant"));
    World::Runner().Trigger(DeathActionReplayedEvent{formID, acAction.Tick});
}

void Actor::Reset() noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, Reset, int, TESObjectREFR*);

    s_pReset(this, 0, nullptr);
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

void Actor::DropOrPickUpObject(const Inventory::Entry& arEntry, NiPoint3* apLocation, NiPoint3* apRotation) noexcept
{
    ExtraDataList* pExtraData = GetExtraDataFromItem(arEntry);

    uint32_t objectId = ItemForm(arEntry.BaseId);
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

uint8_t Actor::GetCombatState() const noexcept
{
    using TGetCombatState = uint32_t(Actor*);
    POINTER_SKYRIMSE(TGetCombatState, s_getCombatState, 38556);
    return static_cast<uint8_t>(s_getCombatState(const_cast<Actor*>(this)));
}

static Hooks::Initializer s_actorHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TGetLocation, s_GetActorLocation, 19812);
        FUNC_GetActorLocation = s_GetActorLocation.Get();
        HOOK_ID(38717, &RealRemoveSpell, HookRemoveSpell);
        HOOK_ID(40245, &RealCharacterConstructor, HookCharacterConstructor);
        HOOK_ID(40246, &RealCharacterConstructor2, HookCharacterConstructor2);
    });
