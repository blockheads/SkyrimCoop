// SkyrimBridge: this game's player (IPlayerBridge).
#include "SkyrimBridgeInternal.h"

uint32_t SkyrimBridge::GetPlayerFormId() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer ? pPlayer->formID : 0;
}

uint32_t SkyrimBridge::GetPlayerBaseNpcFormId() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer && pPlayer->baseForm)
        return pPlayer->baseForm->formID;
    return 0;
}

void SkyrimBridge::GetPlayerPosition(float& aX, float& aY, float& aZ) const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer)
    {
        aX = pPlayer->position.x;
        aY = pPlayer->position.y;
        aZ = pPlayer->position.z;
    }
}

uint32_t SkyrimBridge::GetPlayerCellId() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer)
    {
        if (TESObjectCELL* pCell = pPlayer->GetParentCell())
            return pCell->formID;
    }
    return 0;
}

uint32_t SkyrimBridge::GetPlayerParentCellId() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer && pPlayer->parentCell)
        return pPlayer->parentCell->formID;
    return 0;
}

uint32_t SkyrimBridge::GetPlayerWorldSpaceId() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer)
    {
        if (TESWorldSpace* pWS = pPlayer->GetWorldSpace())
            return pWS->formID;
    }
    return 0;
}

void SkyrimBridge::SetPlayerWaypoint(float aX, float aY, float aZ, uint32_t aWorldSpaceFormId) noexcept
{
    Breadcrumbs::Record("SetPlayerWaypoint", aWorldSpaceFormId);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESForm* pWorldOrCell = TESForm::GetById(aWorldSpaceFormId);
    // 40535 reads the form's type without a null check.
    if (!pPlayer || !pWorldOrCell || (pWorldOrCell->formType != FormType::WorldSpace && pWorldOrCell->formType != FormType::Cell))
        return;
    NiPoint3 pos;
    pos.x = aX;
    pos.y = aY;
    pos.z = aZ;
    pPlayer->SetWaypoint(&pos, pWorldOrCell);
}

void SkyrimBridge::RemovePlayerWaypoint() noexcept
{
    Breadcrumbs::Record("RemovePlayerWaypoint", 0);
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->RemoveWaypoint();
}

bool SkyrimBridge::IsPlayerAlive() const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return !pPlayer->IsDead();
    return false;
}

bool SkyrimBridge::IsPlayerMounted() const noexcept
{
    const auto* pPlayer = PlayerCharacter::Get();
    return pPlayer && pPlayer->extraData.Contains(ExtraDataType::Interaction);
}

void SkyrimBridge::PlayerStopRiding() noexcept
{
    if (auto* pPlayer = PlayerCharacter::Get())
        pPlayer->StopRidingQuick();
}

bool SkyrimBridge::PlayerSitOnMountQuick(uint32_t aMountFormId) noexcept
{
    Breadcrumbs::Record("PlayerSitOnMountQuick", aMountFormId);
    auto* pPlayer = PlayerCharacter::Get();
    Actor* pMount = GetActor(aMountFormId);
    return pPlayer && pMount && pPlayer->SitOnMountQuick(pMount);
}

void SkyrimBridge::PlayerTakeReins(uint32_t aMountFormId) noexcept
{
    Breadcrumbs::Record("PlayerTakeReins", aMountFormId);
    auto* pPlayer = PlayerCharacter::Get();
    Actor* pMount = GetActor(aMountFormId);
    if (!pPlayer || !pMount)
        return;
    pMount->EndMountPair();
    pMount->HoldRider(pPlayer, true);
    pPlayer->StartedRiding(pMount);
    pMount->EvaluatePackageNow();
}

void SkyrimBridge::HoldPlayerDeathReload(bool aHold) noexcept
{
    Breadcrumbs::Record("HoldPlayerDeathReload", static_cast<uint32_t>(aHold));
    PlayerCharacter::HoldDeathReload(aHold);
    // Older builds saved the player essential (going down instead of dying in a session).
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get(); aHold && pPlayer)
        pPlayer->SetEssentialEx(false);
}

void SkyrimBridge::RevivePlayer() noexcept
{
    Breadcrumbs::Record("RevivePlayer", 0);
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->Revive();
}

void SkyrimBridge::EndPlayerRagdoll() noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        QueueRagdollOutOfWorld(*pPlayer);
}

uint32_t SkyrimBridge::GetDifficulty() const noexcept
{
    return static_cast<uint32_t>(*Settings::GetDifficulty());
}

void SkyrimBridge::SetDifficulty(uint32_t aDifficulty) noexcept
{
    Breadcrumbs::Record("SetDifficulty", aDifficulty);
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->SetDifficulty(static_cast<int32_t>(aDifficulty));
}

float SkyrimBridge::GetGreetDistance() const noexcept
{
    const float* pDist = Settings::GetGreetDistance();
    return pDist ? *pDist : 0.f;
}

void SkyrimBridge::SetGreetDistance(float aDistance) noexcept
{
    Breadcrumbs::Record("SetGreetDistance", 0);
    float* pDist = Settings::GetGreetDistance();
    if (pDist)
        *pDist = aDistance;
}

uint16_t SkyrimBridge::GetPlayerLevel() const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return pPlayer->GetLevel();
    return 0;
}

float SkyrimBridge::GetPlayerActorValue(int32_t aIndex) const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return pPlayer->GetActorValue(aIndex);
    return 0.f;
}

void SkyrimBridge::ForcePlayerActorValue(int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    Breadcrumbs::Record("ForcePlayerActorValue", static_cast<uint32_t>(aForceMode), static_cast<uint32_t>(aIndex));
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->ForceActorValue(static_cast<ActorValueOwner::ForceMode>(aForceMode), aIndex, aValue);
}

uint32_t SkyrimBridge::GetPlayerActiveMagicItemFormId(uint32_t aSlot) const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return pPlayer->magicItems[aSlot] ? pPlayer->magicItems[aSlot]->formID : 0;
    return 0;
}

uint32_t SkyrimBridge::GetPlayerEquippedShoutFormId() const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return pPlayer->equippedShout ? pPlayer->equippedShout->formID : 0;
    return 0;
}

void SkyrimBridge::EquipSpell(uint32_t aSpellFormId, uint32_t aSlot) noexcept
{
    Breadcrumbs::Record("EquipSpell", aSpellFormId, aSlot);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESForm* pSpell = TESForm::GetById(aSpellFormId);
    if (pPlayer && pSpell)
        EquipManager::Get()->EquipSpell(pPlayer, pSpell, static_cast<int32_t>(aSlot));
}

void SkyrimBridge::EquipShout(uint32_t aShoutFormId) noexcept
{
    Breadcrumbs::Record("EquipShout", aShoutFormId);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESForm* pShout = TESForm::GetById(aShoutFormId);
    if (pPlayer && pShout)
        EquipManager::Get()->EquipShout(pPlayer, pShout);
}

uint32_t SkyrimBridge::GetPlayerRaceFormId() const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return pPlayer->race ? pPlayer->race->formID : 0;
    return 0;
}

std::vector<uint32_t> SkyrimBridge::GetPlayerActiveQuestFormIds() const noexcept
{
    std::vector<uint32_t> ids;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return ids;
    for (auto& objective : pPlayer->objectives)
        if (objective.instance && objective.instance->quest)
            ids.push_back(objective.instance->quest->formID);
    return ids;
}

bool SkyrimBridge::IsPlayerLoaded() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer && pPlayer->GetNiNode() != nullptr;
}

bool SkyrimBridge::ConsumeTeleported() noexcept
{
    return PlayerCharacter::ConsumeTeleported();
}

float SkyrimBridge::GetPlayerHeading() const noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer ? pPlayer->rotation.z : 0.f;
}

void SkyrimBridge::SetPlayerHeading(float aHeading) noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->SetRotation(pPlayer->rotation.x, 0.f, aHeading);
}

void SkyrimBridge::SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept
{
    PlayerCollision::SetPassThrough(std::vector<uint32_t>(acActorFormIds.begin(), acActorFormIds.end()));
}

void SkyrimBridge::StopTeleportEvents() noexcept
{
    PlayerCharacter::StopTeleportEvents();
}

void SkyrimBridge::ShowHudMessage(const std::string& aMessage) noexcept
{
    Utils::ShowHudMessage(std::string(aMessage));
}

// Worded as the engine's inventory notice (CommonLibSSE-NG SendHUDMessage::ShowInventoryChangeMessage).
void SkyrimBridge::ShowItemChangeMessage(uint32_t aItemFormId, int32_t aCount) noexcept
{
    POINTER_SKYRIMSE(Setting, s_added, 376348);   // sAddItemtoInventory
    POINTER_SKYRIMSE(Setting, s_removed, 376351); // sRemoveItemfromInventory
    const TESForm* pItem = TESForm::GetById(aItemFormId);
    const char* pName = pItem ? pItem->GetName() : nullptr;
    if (!pName || !aCount)
        return;
    const auto* pPhrase = reinterpret_cast<const char*>((aCount > 0 ? s_added.Get() : s_removed.Get())->data);
    const int64_t cCount = std::abs(static_cast<int64_t>(aCount));
    Utils::ShowHudMessage(cCount == 1 ? std::format("{} {}", pName, pPhrase) : std::format("{} ({}) {}", pName, cCount, pPhrase));
}

void SkyrimBridge::ShowEnemyHealth(uint32_t aActorFormId) noexcept
{
    // Posts the HUD's enemy-meter message (HUDData type 8 with the actor's handle); the engine
    // sends it when the player's hit lowers an actor's health (38492). The meter shows the actor
    // if the player is its combat target or its health is damaged (EnemyHealth 51672).
    Actor* pActor = GetActor(aActorFormId);
    if (!pActor)
        return;
    using TShowEnemyHealth = void(const BSPointerHandle<TESObjectREFR>*);
    POINTER_SKYRIMSE(TShowEnemyHealth, s_showEnemyHealth, 52844);
    const auto cHandle = pActor->GetHandle();
    s_showEnemyHealth(&cHandle);
}

std::string SkyrimBridge::GetPlayerLocationName() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return {};
    auto* pLocation = pPlayer->locationForm;
    if (!pLocation)
        return {};
    const char* pName = pLocation->GetName();
    return pName ? pName : std::string{};
}

std::string SkyrimBridge::GetPlayerWorldSpaceName() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return {};
    TESWorldSpace* pWorldspace = pPlayer->GetWorldSpace();
    if (!pWorldspace)
        return {};
    if (pWorldspace->fullName.value.data)
        return pWorldspace->fullName.value.AsAscii();
    return {};
}

Tints SkyrimBridge::GetPlayerFaceTints() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return {};
    return GetActorFaceTints(pPlayer->formID);
}

uint32_t SkyrimBridge::GetPlayerParentCellFormId() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || !pPlayer->parentCell)
        return 0;
    return pPlayer->parentCell->formID;
}

bool SkyrimBridge::IsActorBleedingOut(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    return pActor->actorState.IsBleedingOut();
}

void SkyrimBridge::HoldDeathControls(bool aHold) noexcept
{
    PartyNatives::HoldDeathControls(aHold);
}

bool SkyrimBridge::IsScriptCameraHeld() const noexcept
{
    return PartyNatives::ScriptControlsHeld();
}

void SkyrimBridge::KeepSpectatorView() noexcept
{
    if (PlayerCamera* pCamera = PlayerCamera::Get())
        pCamera->KeepSpectating();
}

void SkyrimBridge::SetSpectatorTarget(uint32_t aFormId) noexcept
{
    PlayerCamera* pCamera = PlayerCamera::Get();
    Actor* pWatched = aFormId ? GetActor(aFormId) : nullptr;
    if (pCamera && (pWatched || !aFormId))
        pCamera->Spectate(pWatched);
}

bool SkyrimBridge::OpenFollowerInventory(uint32_t aNpcFormId) noexcept { return FollowerTargets::OpenInventory(aNpcFormId); }
