// SkyrimBridge: forms and references (IFormBridge).
#include "SkyrimBridgeInternal.h"

bool SkyrimBridge::FormExists(uint32_t aFormId) const noexcept
{
    return TESForm::GetById(aFormId) != nullptr;
}

uint8_t SkyrimBridge::GetFormType(uint32_t aFormId) const noexcept
{
    if (TESForm* pForm = TESForm::GetById(aFormId))
        return static_cast<uint8_t>(pForm->formType);
    return 0;
}

bool SkyrimBridge::IsTwoHandedWeapon(uint32_t aFormId) const noexcept
{
    // Its equip slot covers both hands (BGSEquipSlot::Covers 23605), as AddEquippedObject (39192) fills them.
    using TCovers = bool(const TESForm*, const TESForm*);
    POINTER_SKYRIMSE(TCovers, s_covers, 23605);
    const auto* pWeapon = Cast<TESObjectWEAP>(TESForm::GetById(aFormId));
    const auto& cObjects = DefaultObjectManager::Get();
    return pWeapon && pWeapon->equipSlot && s_covers(pWeapon->equipSlot, cObjects.leftEquipSlot) &&
           s_covers(pWeapon->equipSlot, cObjects.rightEquipSlot);
}

std::string SkyrimBridge::GetFormName(uint32_t aFormId) const noexcept
{
    if (TESForm* pForm = TESForm::GetById(aFormId))
        return pForm->GetName();
    return {};
}

float SkyrimBridge::GetGlobalValue(uint32_t aFormId) const noexcept
{
    if (TESGlobal* pGlobal = Cast<TESGlobal>(TESForm::GetById(aFormId)))
        return pGlobal->f;
    return 0.f;
}

void SkyrimBridge::SetGlobalValue(uint32_t aFormId, float aValue) noexcept
{
    Breadcrumbs::Record("SetGlobalValue", aFormId);
    if (TESGlobal* pGlobal = Cast<TESGlobal>(TESForm::GetById(aFormId)))
        pGlobal->f = aValue;
}

uint32_t SkyrimBridge::LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept
{
    TESWorldSpace* pWS = Cast<TESWorldSpace>(TESForm::GetById(aWorldSpaceFormId));
    if (!pWS)
        return 0;
    TESObjectCELL* pCell = pWS->LoadCell(aX, aY);
    return pCell ? pCell->formID : 0;
}

uint32_t SkyrimBridge::LookupWorldSpaceByFormId(uint32_t aFormId) const noexcept
{
    // WorldSpace lookup by form ID is identity — the form IS the worldspace
    TESWorldSpace* pWS = Cast<TESWorldSpace>(TESForm::GetById(aFormId));
    return pWS ? pWS->formID : 0;
}

bool SkyrimBridge::QuestExists(uint32_t aFormId) const noexcept
{
    return Cast<TESQuest>(TESForm::GetById(aFormId)) != nullptr;
}

uint32_t SkyrimBridge::GetQuestStage(uint32_t aFormId) const noexcept
{
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        return pQ->currentStage;
    return 0;
}

bool SkyrimBridge::IsQuestStopped(uint32_t aFormId) const noexcept
{
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        return pQ->IsStopped();
    return false;
}

uint8_t SkyrimBridge::GetQuestType(uint32_t aFormId) const noexcept
{
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        return static_cast<uint8_t>(pQ->type);
    return 0;
}

std::string SkyrimBridge::GetQuestName(uint32_t aFormId) const noexcept
{
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        return pQ->fullName.value.AsAscii();
    return {};
}

void SkyrimBridge::SetQuestActive(uint32_t aFormId, bool aActive) noexcept
{
    Breadcrumbs::Record("SetQuestActive", aFormId, static_cast<uint32_t>(aActive));
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        pQ->SetActive(aActive);
}

void SkyrimBridge::ScriptSetQuestStage(uint32_t aFormId, uint32_t aStage) noexcept
{
    Breadcrumbs::Record("ScriptSetQuestStage", aFormId, aStage);
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        pQ->ScriptSetStage(static_cast<uint16_t>(aStage));
}

void SkyrimBridge::SetQuestStopped(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("SetQuestStopped", aFormId);
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        pQ->SetStopped();
}

bool SkyrimBridge::IsQuestStagesEmpty(uint32_t aFormId) const noexcept
{
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        return pQ->stages.Empty();
    return true;
}

uint32_t SkyrimBridge::FindQuestByNameId(const std::string& aNameId) const noexcept
{
    auto& questRegistry = ModManager::Get()->quests;
    for (auto* pQ : questRegistry)
    {
        if (pQ && pQ->idName.AsAscii() && SameName(pQ->idName.AsAscii(), aNameId))
            return pQ->formID;
    }
    return 0;
}

uint32_t SkyrimBridge::GetModCount() const noexcept
{
    return static_cast<uint32_t>(ModManager::Get()->mods.Size());
}

std::vector<IFormBridge::ModEntry> SkyrimBridge::GetLoadedMods() const noexcept
{
    std::vector<ModEntry> result;
    auto* cpModManager = ModManager::Get();
    if (!cpModManager)
        return result;

    for (auto* pMod : cpModManager->mods)
    {
        if (!pMod || !pMod->IsLoaded())
            continue;

        ModEntry entry;
        entry.Id = pMod->GetId();
        entry.IsLite = pMod->IsLite();
        entry.Filename = pMod->filename;
        result.push_back(std::move(entry));
    }

    return result;
}

uint32_t SkyrimBridge::GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept
{
    return (aModStandardId << 24) | aSpellLocalId;
}

bool SkyrimBridge::IsSpellItem(uint32_t aFormId) const noexcept
{
    return Cast<SpellItem>(TESForm::GetById(aFormId)) != nullptr;
}

bool SkyrimBridge::IsConcentrationSpell(uint32_t aFormId) const noexcept
{
    if (SpellItem* pSpell = Cast<SpellItem>(TESForm::GetById(aFormId)))
        return pSpell->eCastingType == MagicSystem::CastingType::CONCENTRATION;
    return false;
}

bool SkyrimBridge::IsHealingSpell(uint32_t aFormId) const noexcept
{
    if (SpellItem* pSpell = Cast<SpellItem>(TESForm::GetById(aFormId)))
        return pSpell->IsHealingSpell();
    return false;
}

bool SkyrimBridge::IsBuffSpell(uint32_t aFormId) const noexcept
{
    if (MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aFormId)))
        return pSpell->IsBuffSpell();
    return false;
}

bool SkyrimBridge::IsWardSpell(uint32_t aFormId) const noexcept
{
    if (SpellItem* pSpell = Cast<SpellItem>(TESForm::GetById(aFormId)))
        return pSpell->IsWardSpell();
    return false;
}

bool SkyrimBridge::IsInvisibilitySpell(uint32_t aFormId) const noexcept
{
    if (SpellItem* pSpell = Cast<SpellItem>(TESForm::GetById(aFormId)))
        return pSpell->IsInvisibilitySpell();
    return false;
}

bool SkyrimBridge::IsMagicItem(uint32_t aFormId) const noexcept
{
    return Cast<MagicItem>(TESForm::GetById(aFormId)) != nullptr;
}

bool SkyrimBridge::ObjectHasLock(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        return pObj->GetLock() != nullptr;
    return false;
}

bool SkyrimBridge::ObjectIsLocked(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        if (Lock* pLock = pObj->GetLock())
            return pLock->IsLocked();
    return false;
}

uint8_t SkyrimBridge::GetObjectLockLevel(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        if (Lock* pLock = pObj->GetLock())
            return pLock->lockLevel;
    return 0;
}

bool SkyrimBridge::SetObjectLock(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept
{
    Breadcrumbs::Record("SetObjectLock", aObjectFormId, static_cast<uint32_t>(aLocked));
    TESObjectREFR* pObj = GetRefr(aObjectFormId);
    if (!pObj)
        return false;

    Lock* pLock = pObj->GetLock();
    if (!pLock && aLocked)
    {
        pLock = pObj->CreateLock();
        if (!pLock)
        {
            spdlog::error("SkyrimBridge::SetObjectLock — failed to create lock for {:X}", aObjectFormId);
            return false;
        }
    }

    if (pLock)
    {
        pLock->lockLevel = aLockLevel;
        pLock->SetLock(aLocked);
    }

    return true;
}

uint32_t SkyrimBridge::GetObjectParentCellId(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        if (TESObjectCELL* pCell = pObj->GetParentCellEx())
            return pCell->formID;
    return 0;
}

uint8_t SkyrimBridge::GetObjectOpenState(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        return static_cast<uint8_t>(pObj->GetOpenState());
    return 0;
}

bool SkyrimBridge::PoseObjectOpenState(uint32_t aObjectFormId) noexcept
{
    TESObjectREFR* pObject = GetRefr(aObjectFormId);
    if (!pObject || pObject->IsDeleted())
        return true;
    if (pObject->IsDisabled() || !pObject->GetNiNode())
        return false;
    ReferenceStates::PoseOpenState(*pObject);
    return true;
}

bool SkyrimBridge::IsObjectBaseFormType(uint32_t aObjectFormId, uint8_t aFormType) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        if (pObj->baseForm)
            return static_cast<uint8_t>(pObj->baseForm->formType) == aFormType;
    return false;
}

Inventory SkyrimBridge::GetObjectInventory(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        return pObj->GetInventory();
    return {};
}

void SkyrimBridge::SetObjectInventoryRetainingQuestItems(uint32_t aObjectFormId,
                                                           const Inventory& aCurrentInventory,
                                                           const Inventory& aNewInventory) noexcept
{
    Breadcrumbs::Record("SetObjectInventoryRetainingQuestItems", aObjectFormId);
    TESObjectREFR* pObj = GetRefr(aObjectFormId);
    if (!pObj)
        return;
    Inventory current = aCurrentInventory; // SetInventoryRetainingQuestItems takes non-const ref
    pObj->SetInventoryRetainingQuestItems(current, aNewInventory);
}

void SkyrimBridge::SetObjectInventory(uint32_t aObjectFormId, const Inventory& aInventory) noexcept
{
    Breadcrumbs::Record("SetObjectInventory", aObjectFormId);
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        pObj->SetInventory(aInventory);
}

bool SkyrimBridge::IsRightHandEquipSlot(uint32_t aEquipSlotFormId) const noexcept
{
    TESForm* pSlot = TESForm::GetById(aEquipSlotFormId);
    return pSlot && (pSlot == DefaultObjectManager::Get().rightEquipSlot);
}

void SkyrimBridge::LaunchProjectile(uint32_t aShooterFormId, uint32_t aCellFormId,
                                     float aOriginX, float aOriginY, float aOriginZ,
                                     uint32_t aProjectileBaseId, uint32_t aWeaponId, uint32_t aAmmoId,
                                     uint32_t aSpellId, uint32_t aCastingSource,
                                     float aZAngle, float aXAngle, float aYAngle,
                                     int32_t aArea, float aPower, float aScale,
                                     bool aAlwaysHit, bool aNoDamageOutsideCombat, bool aAutoAim,
                                     bool aDeferInit, bool aForceConeOfFire,
                                     bool aUnkBool1, bool aUnkBool2) noexcept
{
    Breadcrumbs::Record("LaunchProjectile", aShooterFormId, aCellFormId);
    TESObjectCELL* pParentCell = Cast<TESObjectCELL>(TESForm::GetById(aCellFormId));
    if (!pParentCell)
    {
        spdlog::warn("SkyrimBridge::LaunchProjectile — invalid parent cell {:X}", aCellFormId);
        return;
    }

    TESObjectREFR* pShooter = Cast<TESObjectREFR>(TESForm::GetById(aShooterFormId));

    Projectile::LaunchData launchData{};
    launchData.pParentCell = pParentCell;
    launchData.pShooter = pShooter;
    launchData.Origin.x = aOriginX;
    launchData.Origin.y = aOriginY;
    launchData.Origin.z = aOriginZ;
    launchData.pProjectileBase = TESForm::GetById(aProjectileBaseId);
    launchData.pFromWeapon = Cast<TESObjectWEAP>(TESForm::GetById(aWeaponId));
    launchData.pFromAmmo = Cast<TESAmmo>(TESForm::GetById(aAmmoId));
    launchData.fZAngle = aZAngle;
    launchData.fXAngle = aXAngle;
    launchData.fYAngle = aYAngle;
    launchData.pSpell = Cast<MagicItem>(TESForm::GetById(aSpellId));
    launchData.eCastingSource = static_cast<MagicSystem::CastingSource>(aCastingSource);
    launchData.iArea = aArea;
    launchData.fPower = aPower;
    launchData.fScale = aScale;
    launchData.bAlwaysHit = aAlwaysHit;
    launchData.bNoDamageOutsideCombat = aNoDamageOutsideCombat;
    launchData.bAutoAim = aAutoAim;
    launchData.bForceConeOfFire = aForceConeOfFire;
    launchData.bUseOrigin = true; // always use origin to avoid desync
    launchData.bUnkBool1 = aUnkBool1;
    launchData.bUnkBool2 = aUnkBool2;

    BSPointerHandle<Projectile> result;
    Projectile::Launch(&result, launchData);
}

Inventory SkyrimBridge::GetActorEquipment(uint32_t aActorFormId) const noexcept
{
    if (Actor* pActor = GetActor(aActorFormId))
        return pActor->GetEquipment();
    return {};
}

void SkyrimBridge::ActorDropOrPickUpObject(uint32_t aActorFormId, const Inventory::Entry& aItem) noexcept
{
    Breadcrumbs::Record("ActorDropOrPickUpObject", aActorFormId);
    if (Actor* pActor = GetActor(aActorFormId))
        pActor->DropOrPickUpObject(aItem, nullptr, nullptr);
}

int32_t SkyrimBridge::TagStackInPlace(uint32_t aObjectFormId, uint32_t aItemFormId, const std::string& acName, int32_t aCount) noexcept
{
    TESObjectREFR* pHolder = GetRefr(aObjectFormId);
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(aItemFormId));
    return pHolder && pItem ? ::TagStackInPlace(*pHolder, *pItem, acName.c_str(), aCount) : 0;
}

void SkyrimBridge::ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept
{
    Breadcrumbs::Record("ObjectAddOrRemoveItem", aObjectFormId);
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        pObj->AddOrRemoveItem(aItem);
}

void SkyrimBridge::SetCopyItemCount(uint32_t aActorFormId, const GameId& acItem, int32_t aCount, const std::vector<Inventory::Entry>& acStacks) noexcept
{
    Breadcrumbs::Record("SetCopyItemCount", aActorFormId);
    Actor* pActor = GetActor(aActorFormId);
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ItemForm(acItem)));
    if (pActor && pItem)
        SetCarriedStacks(*pActor, *pItem, aCount, acStacks);
}

Inventory SkyrimBridge::GetActorWornArmor(uint32_t aActorFormId) const noexcept
{
    if (Actor* pActor = GetActor(aActorFormId))
        return pActor->GetWornArmor();
    return {};
}

bool SkyrimBridge::ActorIsWearingBodyPiece(uint32_t aActorFormId) const noexcept
{
    if (Actor* pActor = GetActor(aActorFormId))
        return pActor->IsWearingBodyPiece();
    return false;
}

bool SkyrimBridge::ActorShouldWearBodyPiece(uint32_t aActorFormId) const noexcept
{
    if (Actor* pActor = GetActor(aActorFormId))
        return pActor->ShouldWearBodyPiece();
    return false;
}

void SkyrimBridge::ActorResetInventory(uint32_t aActorFormId, bool aBroadcast) noexcept
{
    Breadcrumbs::Record("ActorResetInventory", aActorFormId, static_cast<uint32_t>(aBroadcast));
    if (TESObjectREFR* pObj = GetRefr(aActorFormId))
        pObj->ResetInventory(aBroadcast);
}

void SkyrimBridge::ActorUnequipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept
{
    Breadcrumbs::Record("ActorUnequipSpell", aActorFormId, aSpellFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pSpell = TESForm::GetById(aSpellFormId);
    if (pActor && pSpell)
        EquipManager::Get()->UnEquipSpell(pActor, pSpell, static_cast<int>(aSlotId));
}

void SkyrimBridge::ActorUnequipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept
{
    Breadcrumbs::Record("ActorUnequipShout", aActorFormId, aShoutFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pShout = TESForm::GetById(aShoutFormId);
    if (pActor && pShout)
        EquipManager::Get()->UnEquipShout(pActor, pShout);
}

void SkyrimBridge::ActorResetEquipment(uint32_t aActorFormId, const MagicEquipment& acMagic) noexcept
{
    if (auto* pActor = GetActor(aActorFormId))
    {
        pActor->UnEquipAll();
        pActor->SetMagicEquipment(acMagic);
    }
}

void SkyrimBridge::ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId,
                                    uint32_t aEquipSlotFormId, int32_t aCount) noexcept
{
    Breadcrumbs::Record("ActorEquipItem", aActorFormId, aItemFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pItem = TESForm::GetById(aItemFormId);
    TESForm* pSlot = aEquipSlotFormId ? TESForm::GetById(aEquipSlotFormId) : nullptr;
    if (pActor && pItem)
        EquipManager::Get()->Equip(pActor, pItem, nullptr, aCount, pSlot, false, true, false, false);
}

void SkyrimBridge::ActorWearStack(uint32_t aActorFormId, const Inventory::Entry& acStack) noexcept
{
    Actor* pActor = GetActor(aActorFormId);
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ItemForm(acStack.BaseId)));
    if (!pActor || !pItem)
        return;
    // 38894 with abForceEquip off: no ExtraCannotWear (16066), as Actor.EquipItem's default.
    EquipManager::Get()->Equip(pActor, pItem, FindNamedList(*pActor, *pItem, acStack.ExtraName), 1, nullptr, false, false, true, false);
}

void SkyrimBridge::ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId,
                                      uint32_t aEquipSlotFormId, int32_t aCount) noexcept
{
    Breadcrumbs::Record("ActorUnequipItem", aActorFormId, aItemFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pItem = TESForm::GetById(aItemFormId);
    TESForm* pSlot = aEquipSlotFormId ? TESForm::GetById(aEquipSlotFormId) : nullptr;
    if (pActor && pItem)
        EquipManager::Get()->UnEquip(pActor, pItem, nullptr, aCount, pSlot, false, true, false, false, nullptr);
}

bool SkyrimBridge::IsBoundWeaponSpell(uint32_t aFormId) const noexcept
{
    SpellItem* pSpell = Cast<SpellItem>(TESForm::GetById(aFormId));
    return pSpell ? pSpell->IsBoundWeaponSpell() : false;
}

uint32_t SkyrimBridge::GetSpellCastingType(uint32_t aFormId) const noexcept
{
    SpellItem* pSpell = Cast<SpellItem>(TESForm::GetById(aFormId));
    if (!pSpell)
        return 0;
    return static_cast<uint32_t>(pSpell->eCastingType);
}

bool SkyrimBridge::IsSpellEffectWerewolf(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept
{
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    if (!pSpell)
        return false;
    EffectItem* pEffect = pSpell->GetEffect(aEffectFormId);
    return pEffect ? pEffect->IsWerewolfEffect() : false;
}

bool SkyrimBridge::IsSpellEffectVampireLord(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept
{
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    if (!pSpell)
        return false;
    EffectItem* pEffect = pSpell->GetEffect(aEffectFormId);
    return pEffect ? pEffect->IsVampireLordEffect() : false;
}

bool SkyrimBridge::IsSpellEffectSlow(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept
{
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    if (!pSpell)
        return false;
    EffectItem* pEffect = pSpell->GetEffect(aEffectFormId);
    return pEffect ? pEffect->IsSlowEffect() : false;
}

void SkyrimBridge::AddMagicTargetEffect(uint32_t aTargetFormId, uint32_t aCasterFormId,
                                         uint32_t aSpellFormId, uint32_t aEffectFormId,
                                         float aMagnitude, bool aIsDualCasting,
                                         bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept
{
    Breadcrumbs::Record("AddMagicTargetEffect", aTargetFormId, aCasterFormId);
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    if (!pSpell)
        return;
    EffectItem* pEffect = pSpell->GetEffect(aEffectFormId);
    if (!pEffect)
        return;

    Actor* pTarget = GetActor(aTargetFormId);
    if (!pTarget)
        return;

    Actor* pCaster = aCasterFormId ? GetActor(aCasterFormId) : nullptr;

    // For slow effects, redirect to player
    if (pEffect->IsSlowEffect())
        pTarget = PlayerCharacter::Get();

    if (pEffect->IsWerewolfEffect() || pEffect->IsVampireLordEffect())
        pTarget->GetExtension()->GraphDescriptorHash = 0;

    MagicTarget::AddTargetData data{};
    data.pCaster = pCaster;
    data.pSpell = pSpell;
    data.pEffectItem = pEffect;
    data.fMagnitude = aMagnitude;
    data.fUnkFloat1 = 1.0f;
    data.eCastingSource = MagicSystem::CastingSource::CASTING_SOURCE_COUNT;
    data.bDualCast = aIsDualCasting;

    pTarget->magicTarget.AddTarget(data, aApplyHealPerkBonus, aApplyStaminaPerkBonus);
}

bool SkyrimBridge::IsLoadDoor(uint32_t aObjectFormId) const noexcept
{
    const TESObjectREFR* pObject = GetRefr(aObjectFormId);
    return pObject && pObject->baseForm && pObject->baseForm->formType == FormType::Door &&
           pObject->extraData.Contains(ExtraDataType::Teleport);
}
