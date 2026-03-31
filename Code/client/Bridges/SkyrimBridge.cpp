/// SkyrimBridge.cpp — Real SKSE-backed implementation.
///
/// This file is the ONLY place in the bridge layer that includes SKSE headers.
/// It is compiled as part of SkyrimTogetherClient only, never into headless
/// test targets.

#include <Bridges/SkyrimBridge.h>

// SKSE / game headers (allowed only here)
#include <Actor.h>
#include <PlayerCharacter.h>
#include <Forms/TESForm.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWeather.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESNPC.h>
#include <Forms/SpellItem.h>
#include <Games/ActorExtension.h>
#include <Games/Overrides.h>
#include <Games/References.h>
#include <Sky/Sky.h>
#include <EquipManager.h>
#include <Magic/ActorMagicCaster.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
static Actor* GetActor(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}
} // namespace

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

SkyrimBridge::~SkyrimBridge()
{
    StopRecording();
}

// ---------------------------------------------------------------------------
// NDJSON recording infrastructure
// ---------------------------------------------------------------------------

void SkyrimBridge::StartRecording(const std::string& aPath) noexcept
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    if (m_recording)
        return;

    m_recordFile.open(aPath, std::ios::out | std::ios::trunc);
    if (!m_recordFile.is_open())
    {
        spdlog::error("SkyrimBridge::StartRecording — failed to open '{}'", aPath);
        return;
    }

    m_startTime = std::chrono::steady_clock::now();
    m_recording = true;
    spdlog::info("SkyrimBridge: recording started → {}", aPath);
}

void SkyrimBridge::StopRecording() noexcept
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    if (!m_recording)
        return;

    m_recordFile.flush();
    m_recordFile.close();
    m_recording = false;
    spdlog::info("SkyrimBridge: recording stopped");
}

void SkyrimBridge::RecordCall(const char* apOp, const char* apPayloadJson) noexcept
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    if (!m_recording || !m_recordFile.is_open())
        return;

    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_startTime).count();

    // Write NDJSON line: {"op":"<op>","dir":"call","ts":<seconds>,...}
    m_recordFile << "{\"op\":\"" << apOp << "\",\"dir\":\"call\",\"ts\":"
                 << elapsed << "," << (apPayloadJson ? apPayloadJson : "\"args\":{}") << "}\n";
}

void SkyrimBridge::RecordReturn(const char* apOp, const char* apResultJson) noexcept
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    if (!m_recording || !m_recordFile.is_open())
        return;

    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_startTime).count();

    m_recordFile << "{\"op\":\"" << apOp << "\",\"dir\":\"return\",\"ts\":"
                 << elapsed << "," << (apResultJson ? apResultJson : "\"result\":null") << "}\n";
}

// ---------------------------------------------------------------------------
// IActorBridge
// ---------------------------------------------------------------------------

bool SkyrimBridge::ActorExists(uint32_t aFormId) const noexcept
{
    return GetActor(aFormId) != nullptr;
}

bool SkyrimBridge::IsActorDead(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->IsDead();
    return false;
}

bool SkyrimBridge::IsPlayerSummon(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->IsPlayerSummon();
    return false;
}

bool SkyrimBridge::IsTemporaryActor(uint32_t aFormId) const noexcept
{
    // TODO(02.5-02): implement via Actor flags
    spdlog::warn("SkyrimBridge::IsTemporaryActor not yet implemented for {:X}", aFormId);
    return false;
}

bool SkyrimBridge::IsRemotePlayer(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            return pExt->IsRemotePlayer();
    return false;
}

void SkyrimBridge::SetActorRemote(uint32_t aFormId, bool aRemote) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            pExt->SetRemote(aRemote);
}

void SkyrimBridge::SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            pExt->SetPlayer(aPlayer);
}

uint32_t SkyrimBridge::GetCommandingActor(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        auto owner = pActor->GetCommandingActor();
        return owner ? owner->formID : 0;
    }
    return 0;
}

void SkyrimBridge::KillActor(uint32_t aFormId) noexcept
{
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::KillActor not yet implemented for {:X}", aFormId);
}

void SkyrimBridge::RespawnActor(uint32_t aFormId) noexcept
{
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::RespawnActor not yet implemented for {:X}", aFormId);
}

void SkyrimBridge::DeleteActor(uint32_t aFormId) noexcept
{
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::DeleteActor not yet implemented for {:X}", aFormId);
}

void SkyrimBridge::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    Actor* pActor = GetActor(aFormId);
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(aCellId));
    if (pActor && pCell)
        pActor->MoveTo(pCell, {aX, aY, aZ});
}

void SkyrimBridge::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    // TODO(02.5-02): implement via actor rotation fields
    spdlog::warn("SkyrimBridge::SetActorRotation not yet implemented for {:X}", aFormId);
}

void SkyrimBridge::GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        aX = pActor->position.x;
        aY = pActor->position.y;
        aZ = pActor->position.z;
    }
}

void SkyrimBridge::GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        aX = pActor->rotation.x;
        aY = pActor->rotation.y;
        aZ = pActor->rotation.z;
    }
}

float SkyrimBridge::GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->GetActorValue(aIndex);
    return 0.f;
}

float SkyrimBridge::GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->GetActorPermanentValue(aIndex);
    return 0.f;
}

void SkyrimBridge::SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept
{
    // TODO(02.5-02): implement via ActorValueOwner
    spdlog::warn("SkyrimBridge::SetActorValue not yet implemented for {:X}", aFormId);
}

void SkyrimBridge::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        pActor->ForceActorValue(static_cast<ActorValueOwner::ForceMode>(aForceMode), aIndex, aValue);
}

int32_t SkyrimBridge::GetActorValueCount() const noexcept
{
    return ActorValueInfo::kActorValueCount;
}

void SkyrimBridge::EquipItem(uint32_t aActorFormId, uint32_t aItemFormId, bool aUnequipSlot) noexcept
{
    // TODO(02.5-02): implement via EquipManager
    spdlog::warn("SkyrimBridge::EquipItem not yet implemented");
}

void SkyrimBridge::UnequipItem(uint32_t aActorFormId, uint32_t aItemFormId) noexcept
{
    // TODO(02.5-02): implement via EquipManager
    spdlog::warn("SkyrimBridge::UnequipItem not yet implemented");
}

void SkyrimBridge::DrawWeapon(uint32_t aFormId, bool aDraw) noexcept
{
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::DrawWeapon not yet implemented");
}

bool SkyrimBridge::IsWeaponDrawn(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->actorState.IsWeaponDrawn();
    return false;
}

uint32_t SkyrimBridge::GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept
{
    // TODO(02.5-02): implement
    return 0;
}

uint64_t SkyrimBridge::GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            return pExt->GraphDescriptorHash;
    return 0;
}

void SkyrimBridge::SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            pExt->GraphDescriptorHash = aHash;
}

void SkyrimBridge::GenerateMagicCasters(uint32_t aFormId) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        pActor->GenerateMagicCasters();
}

void SkyrimBridge::SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        using CS = MagicSystem::CastingSource;
        MagicCaster* pCaster = pActor->GetMagicCaster(static_cast<CS>(aCastingSource));
        if (pCaster)
            pCaster->SetDualCasting(aDual);
    }
}

void SkyrimBridge::CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource) noexcept
{
    // TODO(02.5-02): implement via ActorMagicCaster
    spdlog::warn("SkyrimBridge::CastSpell not yet implemented");
}

void SkyrimBridge::InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept
{
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::InterruptCast not yet implemented");
}

void SkyrimBridge::RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept
{
    Actor* pActor = GetActor(aActorFormId);
    if (!pActor)
        return;
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    if (pSpell)
        pActor->RemoveSpell(pSpell);
}

void SkyrimBridge::AddMagicTarget(uint32_t aActorFormId, uint32_t aCasterFormId, uint32_t aTargetFormId,
                                    bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept
{
    // TODO(02.5-02): implement via magicTarget.AddTarget
    spdlog::warn("SkyrimBridge::AddMagicTarget not yet implemented");
}

bool SkyrimBridge::IsBleedingOut(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->actorState.IsBleedingOut();
    return false;
}

void SkyrimBridge::PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        pActor->PayCrimeGoldToAllFactions();
}

uint32_t SkyrimBridge::GetEquippedShoutFormId(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->equippedShout ? pActor->equippedShout->formID : 0;
    return 0;
}

uint32_t SkyrimBridge::GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->magicItems[aSlot] ? pActor->magicItems[aSlot]->formID : 0;
    return 0;
}

uint32_t SkyrimBridge::GetCombatTargetFormId(uint32_t aFormId) const noexcept
{
    // TODO(02.5-02): implement via pCombatController
    return 0;
}

uint32_t SkyrimBridge::GetActorBaseFormId(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->baseForm ? pActor->baseForm->formID : 0;
    return 0;
}

std::string SkyrimBridge::GetActorBaseName(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        if (pActor->baseForm)
            return pActor->baseForm->GetName();
    return {};
}

uint32_t SkyrimBridge::GetActorRaceFormId(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->race ? pActor->race->formID : 0;
    return 0;
}

bool SkyrimBridge::ActorHas3D(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->GetNiNode() != nullptr;
    return false;
}

// ---------------------------------------------------------------------------
// IFormBridge
// ---------------------------------------------------------------------------

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

std::string SkyrimBridge::GetFormEditorId(uint32_t aFormId) const noexcept
{
    // TODO(02.5-02): implement
    return {};
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

uint32_t SkyrimBridge::GetParentCellFormId(uint32_t aObjectFormId) const noexcept
{
    // TODO(02.5-02): implement via TESObjectREFR::parentCell
    return 0;
}

uint32_t SkyrimBridge::GetParentWorldSpaceFormId(uint32_t aObjectFormId) const noexcept
{
    // TODO(02.5-02): implement
    return 0;
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
    if (TESQuest* pQ = Cast<TESQuest>(TESForm::GetById(aFormId)))
        pQ->SetActive(aActive);
}

uint32_t SkyrimBridge::FindQuestByNameId(const std::string& aNameId) const noexcept
{
    auto& questRegistry = ModManager::Get()->quests;
    for (auto* pQ : questRegistry)
    {
        if (pQ && std::strcmp(pQ->idName.AsAscii(), aNameId.c_str()) == 0)
            return pQ->formID;
    }
    return 0;
}

uint32_t SkyrimBridge::GetModCount() const noexcept
{
    return static_cast<uint32_t>(ModManager::Get()->modCount);
}

std::string SkyrimBridge::GetModNameByIndex(uint32_t aIndex) const noexcept
{
    // TODO(02.5-02): implement via ModManager mod list
    return {};
}

uint32_t SkyrimBridge::GetModStandardId(const std::string& aModName) const noexcept
{
    // TODO(02.5-02): implement
    return 0;
}

std::vector<std::string> SkyrimBridge::GetAllModNames() const noexcept
{
    // TODO(02.5-02): implement
    return {};
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

// ---------------------------------------------------------------------------
// IPlayerBridge
// ---------------------------------------------------------------------------

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
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESWorldSpace* pWS = aWorldSpaceFormId
        ? Cast<TESWorldSpace>(TESForm::GetById(aWorldSpaceFormId))
        : nullptr;
    if (pPlayer)
    {
        NiPoint3 pos{aX, aY, aZ};
        pPlayer->SetWaypoint(&pos, pWS);
    }
}

void SkyrimBridge::RemovePlayerWaypoint() noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->RemoveWaypoint();
}

void SkyrimBridge::MovePlayerTo(uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(aCellId));
    if (pPlayer && pCell)
        pPlayer->MoveTo(pCell, {aX, aY, aZ});
}

bool SkyrimBridge::IsPlayerAlive() const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return !pPlayer->IsDead();
    return false;
}

bool SkyrimBridge::IsPlayerBleedingOut() const noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        return pPlayer->actorState.IsBleedingOut();
    return false;
}

bool SkyrimBridge::IsPlayerInGodMode() const noexcept
{
    // TODO(02.5-02): implement via PlayerCharacter god mode flag
    return false;
}

void SkyrimBridge::SetPlayerGodMode(bool aEnable) noexcept
{
    PlayerCharacter::SetGodMode(aEnable);
}

void SkyrimBridge::SetPlayerRespawnMode(bool aEnable) noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->SetPlayerRespawnMode(aEnable);
}

void SkyrimBridge::RespawnPlayer() noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->RespawnPlayer();
}

void SkyrimBridge::KnockdownPlayer(float aForce) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer && pPlayer->currentProcess)
        pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, aForce);
}

void SkyrimBridge::FadeScreen(bool aFadeOut, bool aToBlack, float aFadeDuration,
                               bool aWithSound, float aDelay) noexcept
{
    FadeOutGame(aFadeOut, aToBlack, aFadeDuration, aWithSound, aDelay);
}

void SkyrimBridge::PlayerPayGold(uint32_t aAmount) noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->PayGold(static_cast<int32_t>(aAmount));
}

void SkyrimBridge::PlayerPayCrimeGoldToAllFactions() noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->PayCrimeGoldToAllFactions();
}

uint32_t SkyrimBridge::GetDifficulty() const noexcept
{
    return static_cast<uint32_t>(*Settings::GetDifficulty());
}

void SkyrimBridge::SetDifficulty(uint32_t aDifficulty) noexcept
{
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
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESForm* pSpell = TESForm::GetById(aSpellFormId);
    if (pPlayer && pSpell)
        EquipManager::Get()->EquipSpell(pPlayer, pSpell, static_cast<int32_t>(aSlot));
}

void SkyrimBridge::EquipShout(uint32_t aShoutFormId) noexcept
{
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

void SkyrimBridge::ShowHudMessage(const std::string& aMessage) noexcept
{
    Utils::ShowHudMessage(String(aMessage));
}

// ---------------------------------------------------------------------------
// IEventBridge
// ---------------------------------------------------------------------------

void SkyrimBridge::RegisterQuestStartStopSink(std::function<void(uint32_t, bool)> aCallback) noexcept
{
    // TODO(02.5-02): store callback and register with EventDispatcherManager
    spdlog::warn("SkyrimBridge::RegisterQuestStartStopSink: implement in plan 02/03");
}

void SkyrimBridge::RegisterQuestStageSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept
{
    spdlog::warn("SkyrimBridge::RegisterQuestStageSink: implement in plan 02/03");
}

void SkyrimBridge::RegisterActivateSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept
{
    spdlog::warn("SkyrimBridge::RegisterActivateSink: implement in plan 02/03");
}

void SkyrimBridge::RegisterLockChangeSink(std::function<void(uint32_t, bool)> aCallback) noexcept
{
    spdlog::warn("SkyrimBridge::RegisterLockChangeSink: implement in plan 02/03");
}

void SkyrimBridge::UnregisterAllSinks() noexcept
{
    // TODO(02.5-02): unregister all sinks
}

void SkyrimBridge::InjectQuestStartStopEvent(uint32_t aQuestFormId, bool aStarted) noexcept
{
    // No-op in real SKSE bridge — events arrive via game, not injected
}

void SkyrimBridge::InjectQuestStageEvent(uint32_t aQuestFormId, uint32_t aNewStage) noexcept
{
    // No-op in real SKSE bridge
}

void SkyrimBridge::InjectActivateEvent(uint32_t aActivatorFormId, uint32_t aActivatedFormId) noexcept
{
    // No-op in real SKSE bridge
}

// ---------------------------------------------------------------------------
// IWorldBridge
// ---------------------------------------------------------------------------

uint32_t SkyrimBridge::GetCurrentWeatherId() const noexcept
{
    Sky* pSky = Sky::Get();
    if (!pSky)
        return 0;
    TESWeather* pWeather = pSky->GetWeather();
    return pWeather ? pWeather->formID : 0;
}

uint32_t SkyrimBridge::GetLastWeatherId() const noexcept
{
    // TODO(02.5-02): implement via Sky::lastWeather
    return 0;
}

void SkyrimBridge::ForceWeather(uint32_t aWeatherFormId, bool aOverride) noexcept
{
    // TODO(02.5-02): implement via Sky::ForceWeather
    spdlog::warn("SkyrimBridge::ForceWeather not yet implemented for {:X}", aWeatherFormId);
}

void SkyrimBridge::ReleaseWeatherOverride() noexcept
{
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::ReleaseWeatherOverride not yet implemented");
}

std::string SkyrimBridge::GetModName(uint32_t aIndex) const noexcept
{
    // TODO(02.5-02): implement via Sky/ModManager
    return {};
}

void SkyrimBridge::SetGameWeatherSystemEnabled(bool aEnable) noexcept
{
    // TODO(02.5-02): implement via Sky toggle
    spdlog::warn("SkyrimBridge::SetGameWeatherSystemEnabled({}) not yet implemented", aEnable);
}

float SkyrimBridge::GetGameTimeHours() const noexcept
{
    // TODO(02.5-02): implement via TimeManager or Sky
    return 0.f;
}

void SkyrimBridge::SetGameTimeHours(float aHours) noexcept
{
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::SetGameTimeHours({}) not yet implemented", aHours);
}
