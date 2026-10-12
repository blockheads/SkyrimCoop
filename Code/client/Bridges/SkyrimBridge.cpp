/// SkyrimBridge.cpp — Real SKSE-backed implementation.
///
/// This file is the ONLY place in the bridge layer that includes SKSE headers.
/// It is compiled as part of SkyrimTogetherClient only, never into headless
/// test targets.

#include <Bridges/SkyrimBridge.h>
#include <Breadcrumbs.h>

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
#include <Forms/ActorValueInfo.h>
#include <Forms/SpellItem.h>
#include <Games/ActorExtension.h>
#include <Games/Overrides.h>
#include <Games/References.h>
#include <Games/TES.h>
#include <Sky/Sky.h>
#include <EquipManager.h>
#include <Interface/UI.h>
#include <Magic/ActorMagicCaster.h>
#include <TimeManager.h>
#include <TESObjectREFR.h>
#include <DefaultObjectManager.h>
#include <Projectiles/Projectile.h>
#include <Forms/TESObjectWEAP.h>
#include <Forms/TESAmmo.h>
#include <Misc/BSFixedString.h>
#include <Forms/TESPackage.h>
#include <Forms/TESTopicInfo.h>
#include <Forms/TESRace.h>
#include <Forms/BGSEncounterZone.h>
#include <Services/QuestService.h>
#include <Forms/MagicItem.h>
#include <Magic/MagicTarget.h>
#include <Effects/ActiveEffect.h>
#include <Magic/EffectItem.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_WerewolfBehavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>
#include <Systems/FaceGenSystem.h>
#include <Systems/CacheSystem.h>
#include <Systems/InterpolationSystem.h>
#include <Systems/AnimationSystem.h>
#include <Games/Misc/SubtitleManager.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <Games/Animation/ActorMediator.h>

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
    // Runtime-created references (placeatme, spawned remote players) live in the 0xFF range.
    Actor* pActor = GetActor(aFormId);
    return pActor && pActor->IsTemporary();
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
    Breadcrumbs::Record("SetActorRemote", aFormId, static_cast<uint32_t>(aRemote));
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            pExt->SetRemote(aRemote);
}

void SkyrimBridge::SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept
{
    Breadcrumbs::Record("SetActorPlayer", aFormId, static_cast<uint32_t>(aPlayer));
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
    Breadcrumbs::Record("KillActor", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->Kill();
}

void SkyrimBridge::KillActorBy(uint32_t aFormId, uint32_t aKillerFormId) noexcept
{
    Breadcrumbs::Record("KillActorBy", aFormId, aKillerFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor || pActor->GetExtension()->IsPlayer())
        return;
    pActor->KillImpl(aKillerFormId ? GetActor(aKillerFormId) : nullptr, 100.f, true, true);
}

void SkyrimBridge::RespawnActor(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("RespawnActor", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->Respawn();
}

void SkyrimBridge::ResurrectActor(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("ResurrectActor", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->Resurrect(false);
}

void SkyrimBridge::DeleteActor(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("DeleteActor", aFormId);
    // Engine Delete is for dynamically-created references only. Never delete
    // the local player or an NPC reference that belongs to the save/plugin.
    if ((aFormId & 0xFF000000u) != 0xFF000000u) return;
    if (auto* actor = GetActor(aFormId))
        actor->Delete();
}

void SkyrimBridge::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    Breadcrumbs::Record("MoveActorTo", aFormId, aCellId);
    Actor* pActor = GetActor(aFormId);
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(aCellId));
    if (pActor && pCell)
    {
        NiPoint3 pos;
        pos.x = aX;
        pos.y = aY;
        pos.z = aZ;
        pActor->MoveTo(pCell, pos);
    }
}

void SkyrimBridge::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    Breadcrumbs::Record("SetActorRotation", aFormId);
    // TODO(02.5-02): implement via actor rotation fields
    spdlog::warn("SkyrimBridge::SetActorRotation not yet implemented for {:X}", aFormId);
}

bool SkyrimBridge::ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3],
    const float aRotation[3], float aDirection, const AnimationVariables& aVariables) noexcept
{
    Breadcrumbs::Record("ApplyRemoteMovement", aFormId);
    auto* actor = GetActor(aFormId);
    if (!actor) return false;
    NiPoint3 position;
    position.x = aPosition[0];
    position.y = aPosition[1];
    position.z = aPosition[2];
    actor->ForcePosition(position);
    actor->LoadAnimationVariables(aVariables);
    if (actor->currentProcess && actor->currentProcess->middleProcess)
        actor->currentProcess->middleProcess->direction = aDirection;
    actor->SetRotation(aRotation[0], aRotation[1], aRotation[2]);
    return true;
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
    Breadcrumbs::Record("SetActorValue", aFormId, static_cast<uint32_t>(aIndex));
    if (Actor* pActor = GetActor(aFormId))
        pActor->SetActorValue(static_cast<uint32_t>(aIndex), aValue);
}

void SkyrimBridge::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    Breadcrumbs::Record("ForceActorValue", aFormId, static_cast<uint32_t>(aForceMode));
    if (Actor* pActor = GetActor(aFormId))
        pActor->ForceActorValue(static_cast<ActorValueOwner::ForceMode>(aForceMode), aIndex, aValue);
}

int32_t SkyrimBridge::GetActorValueCount() const noexcept
{
    return ActorValueInfo::kActorValueCount;
}

void SkyrimBridge::EquipItem(uint32_t aActorFormId, uint32_t aItemFormId, bool aUnequipSlot) noexcept
{
    Breadcrumbs::Record("EquipItem", aActorFormId, aItemFormId);
    // TODO(02.5-02): implement via EquipManager
    spdlog::warn("SkyrimBridge::EquipItem not yet implemented");
}

void SkyrimBridge::UnequipItem(uint32_t aActorFormId, uint32_t aItemFormId) noexcept
{
    Breadcrumbs::Record("UnequipItem", aActorFormId, aItemFormId);
    // TODO(02.5-02): implement via EquipManager
    spdlog::warn("SkyrimBridge::UnequipItem not yet implemented");
}

void SkyrimBridge::DrawWeapon(uint32_t aFormId, bool aDraw) noexcept
{
    Breadcrumbs::Record("DrawWeapon", aFormId, static_cast<uint32_t>(aDraw));
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
    // What the actor holds: AIProcess::equippedObjects[0 left, 1 right] at +0xF0 (CommonLibSSE).
    Actor* pActor = GetActor(aActorFormId);
    if (!pActor || !pActor->currentProcess || aSlot > 1)
        return 0;
    const auto* pObjects = reinterpret_cast<TESForm* const*>(reinterpret_cast<const uint8_t*>(pActor->currentProcess) + 0xF0);
    return pObjects[aSlot] ? pObjects[aSlot]->formID : 0;
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
    Breadcrumbs::Record("SetAnimationGraphDescriptorHash", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            pExt->GraphDescriptorHash = aHash;
}

void SkyrimBridge::GenerateMagicCasters(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("GenerateMagicCasters", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->GenerateMagicCasters();
}

void SkyrimBridge::SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept
{
    Breadcrumbs::Record("SetMagicCasterDualCasting", aFormId, aCastingSource);
    if (Actor* pActor = GetActor(aFormId))
    {
        using CS = MagicSystem::CastingSource;
        MagicCaster* pCaster = pActor->GetMagicCaster(static_cast<CS>(aCastingSource));
        if (pCaster)
            pCaster->SetDualCasting(aDual);
    }
}

static MagicCaster* GetCaster(Actor* apActor, uint32_t aCastingSource) noexcept
{
    if (!apActor || aCastingSource >= 4)
        return nullptr;
    apActor->GenerateMagicCasters();
    return apActor->GetMagicCaster(static_cast<MagicSystem::CastingSource>(aCastingSource));
}

void SkyrimBridge::CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                             uint32_t aDesiredTargetFormId) noexcept
{
    Breadcrumbs::Record("CastSpell", aActorFormId, aSpellFormId);
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    MagicCaster* pCaster = GetCaster(GetActor(aActorFormId), aCastingSource);
    if (!pSpell || !pCaster)
    {
        spdlog::warn("{}: no spell {:X} or caster {} for actor {:X}", __FUNCTION__, aSpellFormId, aCastingSource, aActorFormId);
        return;
    }
    TESObjectREFR* pTarget = aDesiredTargetFormId ? Cast<TESObjectREFR>(TESForm::GetById(aDesiredTargetFormId)) : nullptr;
    pCaster->CastSpellImmediate(pSpell, false, pTarget, 1.0f, false, 0.0f);
}

void SkyrimBridge::InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept
{
    Breadcrumbs::Record("InterruptCast", aFormId, aCastingSource);
    if (MagicCaster* pCaster = GetCaster(GetActor(aFormId), aCastingSource))
        pCaster->InterruptCast();
}

uint32_t SkyrimBridge::GetCastingSpellFormId(uint32_t aFormId, uint32_t aCastingSource) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || aCastingSource >= 4 || !pActor->casters[aCastingSource])
        return 0;
    MagicItem* pSpell = pActor->casters[aCastingSource]->pCurrentSpell;
    return pSpell ? pSpell->formID : 0;
}

void SkyrimBridge::RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept
{
    Breadcrumbs::Record("RemoveSpell", aActorFormId, aSpellFormId);
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
    Breadcrumbs::Record("AddMagicTarget", aActorFormId, aCasterFormId);
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
    Breadcrumbs::Record("PayCrimeGoldToAllFactions", aFormId);
    if (Actor* pActor = GetActor(aFormId))
    {
        if (auto* pPlayer = Cast<PlayerCharacter>(pActor))
            pPlayer->PayCrimeGoldToAllFactions();
    }
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
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->pCombatController)
        return 0;
    Actor* pTarget = pActor->GetCombatTarget();
    return pTarget ? pTarget->formID : 0;
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

std::vector<IActorBridge::ActiveEffectInfo> SkyrimBridge::GetActorActiveEffects(uint32_t aFormId) const noexcept
{
    std::vector<ActiveEffectInfo> result;
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return result;

    // MagicTarget::GetActiveEffectList is vtable slot 7 (CommonLibSSE); the list is a
    // BSSimpleList<ActiveEffect*>: an inline head node {item, next}.
    struct Node
    {
        ActiveEffect* pItem;
        Node* pNext;
    };
    MagicTarget* pTarget = &pActor->magicTarget;
    using TGetList = Node* (*)(MagicTarget*);
    const auto cGetList = reinterpret_cast<TGetList>((*reinterpret_cast<void***>(pTarget))[7]);
    for (Node* pNode = cGetList(pTarget); pNode; pNode = pNode->pNext)
    {
        ActiveEffect* pEffect = pNode->pItem;
        if (!pEffect)
            continue;
        ActiveEffectInfo info;
        auto* pEffectItem = static_cast<EffectItem*>(pEffect->pEffect);
        info.EffectFormId = pEffectItem && pEffectItem->pEffectSetting ? pEffectItem->pEffectSetting->formID : 0;
        info.SpellFormId = pEffect->pSpell ? pEffect->pSpell->formID : 0;
        if (TESObjectREFR* pCaster = TESObjectREFR::GetByHandle(pEffect->hCaster))
            info.CasterFormId = pCaster->formID;
        info.Magnitude = pEffect->fMagnitude;
        info.Duration = pEffect->fDuration;
        info.Elapsed = pEffect->fElapsedSeconds;
        info.Flags = pEffect->uiFlags;
        result.push_back(info);
    }
    return result;
}

uint32_t SkyrimBridge::GetKillerFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->IsDead())
        return 0;
    // Killer handle at Actor+0x108 (papyrus Actor.GetKiller, ID 54689 on 1.6.1170).
    const uint32_t cHandle = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(pActor) + 0x108);
    TESObjectREFR* pKiller = TESObjectREFR::GetByHandle(cHandle);
    return pKiller ? pKiller->formID : 0;
}

void SkyrimBridge::ReactToAttack(uint32_t aVictimFormId, uint32_t aAttackerFormId) noexcept
{
    Breadcrumbs::Record("ReactToAttack", aVictimFormId, aAttackerFormId);
    Actor* pVictim = GetActor(aVictimFormId);
    Actor* pAttacker = GetActor(aAttackerFormId);
    if (!pVictim || !pAttacker || pVictim == pAttacker)
        return;

    // A remote player's character carries their factions (PlayerFaction...), so allies and friends
    // of the player see it as one of their own and the engine's reaction below ignores its hits.
    // The engine only tolerates-then-retaliates for its own player, with these settings; apply the
    // same rule to remote players: after too many hits the victim fights them.
    if (pAttacker->GetExtension()->IsRemotePlayer())
    {
        TP_THIS_FUNCTION(TGetFactionReaction, int32_t, Actor, Actor*);
        POINTER_SKYRIMSE(TGetFactionReaction, s_getFactionReaction, 37666);
        const int32_t cReaction = TiltedPhoques::ThisCall(s_getFactionReaction.Get(), pVictim, pAttacker);
        constexpr int32_t cAlly = 2, cFriend = 3;
        if (cReaction == cAlly || cReaction == cFriend)
        {
            VersionDbPtr<int32_t> friendHitNonCombat(370673), friendHitCombat(370670);
            VersionDbPtr<int32_t> allyHitNonCombat(370679), allyHitCombat(370676);
            const bool cInCombat = pVictim->IsInCombat();
            const int32_t cAllowed = cReaction == cFriend ? *(cInCombat ? friendHitCombat : friendHitNonCombat).Get()
                                                          : *(cInCombat ? allyHitCombat : allyHitNonCombat).Get();

            // Hits per (victim, attacker); forgotten after a while, like the engine's friendly hits.
            struct Hits
            {
                int32_t Count{};
                std::chrono::steady_clock::time_point Last{};
            };
            static Map<uint64_t, Hits> s_friendlyHits;
            auto& hits = s_friendlyHits[(uint64_t(aVictimFormId) << 32) | aAttackerFormId];
            const auto cNow = std::chrono::steady_clock::now();
            // One spell arrives as one request per effect: those are the same hit.
            if (cNow - hits.Last < std::chrono::milliseconds(250))
                return;
            if (cNow - hits.Last > std::chrono::seconds(30))
                hits.Count = 0;
            hits.Last = cNow;

            spdlog::debug("ReactToAttack: {:X} hit by remote player {:X} ({}), friendly hit {}/{}", aVictimFormId,
                          aAttackerFormId, cReaction == cFriend ? "friend" : "ally", hits.Count + 1, cAllowed);
            if (++hits.Count <= cAllowed)
                return;

            hits.Count = 0;
            MarkAngryWithRemotePlayer(pVictim, pAttacker);
            spdlog::info("ReactToAttack: {:X} is now angry with remote player {:X}", aVictimFormId, aAttackerFormId);
            // Now hostile: react to this hit the way the engine reacts to an enemy's (below).
        }
    }

    // Called by the engine's hit processing as Attacked(victim, attacker, 0, victim aggression)
    // (IDA: TESObjectREFR::sub_38586 at +0x1352 on 1.6.1170).
    TP_THIS_FUNCTION(TGetAggression, uint32_t, Actor);
    POINTER_SKYRIMSE(TGetAggression, s_getAggression, 37671);
    using TAttacked = void(Actor*, Actor*, uint64_t, uint32_t);
    POINTER_SKYRIMSE(TAttacked, s_attacked, 38626);

    const uint32_t cAggression = TiltedPhoques::ThisCall(s_getAggression.Get(), pVictim);
    s_attacked.Get()(pVictim, pAttacker, 0, cAggression);
    spdlog::debug("ReactToAttack: {:X} attacked by {:X} (aggression {})", aVictimFormId, aAttackerFormId, cAggression);
}

bool SkyrimBridge::IsAnimationGraphReady(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor && EngineVirtual(pActor->animationGraphHolder).IsReady();
}

bool SkyrimBridge::ActorHas3D(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->GetNiNode() != nullptr;
    return false;
}

bool SkyrimBridge::IsActorRemote(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor && pActor->GetExtension()->IsRemote();
}

std::vector<uint32_t> SkyrimBridge::GetLoadedActorFormIds() const noexcept
{
    std::vector<uint32_t> formIds;
    ProcessLists* const pProcessLists = ProcessLists::Get();
    if (!pProcessLists)
        return formIds;

    for (uint32_t i = 0; i < pProcessLists->highActorHandleArray.length; ++i)
    {
        TESObjectREFR* const pRefr = TESObjectREFR::GetByHandle(pProcessLists->highActorHandleArray[i]);
        if (pRefr && pRefr->GetNiNode())
            formIds.push_back(pRefr->formID);
    }
    return formIds;
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

bool SkyrimBridge::IsQuestOverrideActive() const noexcept
{
    return ScopedQuestOverride::IsOverriden();
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
    return static_cast<uint32_t>(ModManager::Get()->mods.Size());
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

// ---------------------------------------------------------------------------
// IFormBridge — object operations (new in plan 02.5-03)
// ---------------------------------------------------------------------------

static TESObjectREFR* GetRefr(uint32_t aFormId) noexcept
{
    return Cast<TESObjectREFR>(TESForm::GetById(aFormId));
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

void SkyrimBridge::NotifyObjectLockChange(uint32_t aObjectFormId) noexcept
{
    Breadcrumbs::Record("NotifyObjectLockChange", aObjectFormId);
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        pObj->LockChange();
}

uint32_t SkyrimBridge::GetObjectParentCellId(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        if (TESObjectCELL* pCell = pObj->GetParentCellEx())
            return pCell->formID;
    return 0;
}

void SkyrimBridge::ActivateObjectFromActor(uint32_t aObjectFormId, uint32_t aActivatorFormId,
                                             uint8_t aPreActivationState) noexcept
{
    Breadcrumbs::Record("ActivateObjectFromActor", aObjectFormId, aActivatorFormId);
    TESObjectREFR* pObject = GetRefr(aObjectFormId);
    Actor* pActor = GetActor(aActivatorFormId);
    if (!pObject || !pActor)
    {
        spdlog::error("SkyrimBridge::ActivateObjectFromActor — object {:X} or actor {:X} not found",
                      aObjectFormId, aActivatorFormId);
        return;
    }

    if (pObject->baseForm && static_cast<uint8_t>(pObject->baseForm->formType) == static_cast<uint8_t>(FormType::Door))
    {
        // A load door would take the copy through it, out of this game's cell. The player's own
        // game moves it, and everyone follows (cell arrivals).
        if (IsLoadDoor(aObjectFormId))
        {
            spdlog::info("Load door {:X} not replayed: its player's own game takes it through", aObjectFormId);
            return;
        }

        auto remotePreActivationState = static_cast<TESObjectREFR::OpenState>(aPreActivationState);
        TESObjectREFR::OpenState localState = pObject->GetOpenState();
        if (remotePreActivationState != localState)
            return;
    }

    pObject->Activate(pActor, 0, nullptr, 1, 0);
}

void SkyrimBridge::PlayObjectAnimation(uint32_t aObjectFormId, const std::string& aAnimation,
                                         const std::string& aEventName) noexcept
{
    Breadcrumbs::Record("PlayObjectAnimation", aObjectFormId);
    TESObjectREFR* pObject = GetRefr(aObjectFormId);
    if (!pObject)
    {
        spdlog::error("SkyrimBridge::PlayObjectAnimation — object {:X} not found", aObjectFormId);
        return;
    }

    BSFixedString eventName(aEventName.c_str());
    if (aAnimation.empty())
    {
        pObject->PlayAnimation(&eventName);
    }
    else
    {
        BSFixedString animation(aAnimation.c_str());
        pObject->PlayAnimationAndWait(&animation, &eventName);
    }
}

uint8_t SkyrimBridge::GetObjectOpenState(uint32_t aObjectFormId) const noexcept
{
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        return static_cast<uint8_t>(pObj->GetOpenState());
    return 0;
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

// ---------------------------------------------------------------------------
// IFormBridge — projectile launch (new in plan 02.5-03)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// IFormBridge — actor inventory/equipment helpers (new in plan 02.5-03)
// ---------------------------------------------------------------------------

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

void SkyrimBridge::ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept
{
    Breadcrumbs::Record("ObjectAddOrRemoveItem", aObjectFormId);
    if (TESObjectREFR* pObj = GetRefr(aObjectFormId))
        pObj->AddOrRemoveItem(aItem);
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

void SkyrimBridge::ActorEquipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept
{
    Breadcrumbs::Record("ActorEquipSpell", aActorFormId, aSpellFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pSpell = TESForm::GetById(aSpellFormId);
    if (pActor && pSpell)
        EquipManager::Get()->EquipSpell(pActor, pSpell, static_cast<int>(aSlotId));
}

void SkyrimBridge::ActorUnequipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept
{
    Breadcrumbs::Record("ActorUnequipSpell", aActorFormId, aSpellFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pSpell = TESForm::GetById(aSpellFormId);
    if (pActor && pSpell)
        EquipManager::Get()->UnEquipSpell(pActor, pSpell, static_cast<int>(aSlotId));
}

void SkyrimBridge::ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept
{
    Breadcrumbs::Record("ActorEquipShout", aActorFormId, aShoutFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pShout = TESForm::GetById(aShoutFormId);
    if (pActor && pShout)
        EquipManager::Get()->EquipShout(pActor, pShout);
}

void SkyrimBridge::ActorUnequipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept
{
    Breadcrumbs::Record("ActorUnequipShout", aActorFormId, aShoutFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pShout = TESForm::GetById(aShoutFormId);
    if (pActor && pShout)
        EquipManager::Get()->UnEquipShout(pActor, pShout);
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
    Breadcrumbs::Record("SetPlayerWaypoint", aWorldSpaceFormId);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESWorldSpace* pWS = aWorldSpaceFormId
        ? Cast<TESWorldSpace>(TESForm::GetById(aWorldSpaceFormId))
        : nullptr;
    if (pPlayer)
    {
        NiPoint3 pos;
        pos.x = aX;
        pos.y = aY;
        pos.z = aZ;
        pPlayer->SetWaypoint(&pos, pWS);
    }
}

void SkyrimBridge::RemovePlayerWaypoint() noexcept
{
    Breadcrumbs::Record("RemovePlayerWaypoint", 0);
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->RemoveWaypoint();
}

void SkyrimBridge::MovePlayerTo(uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    Breadcrumbs::Record("MovePlayerTo", aCellId);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(aCellId));
    if (pPlayer && pCell)
    {
        NiPoint3 pos;
        pos.x = aX;
        pos.y = aY;
        pos.z = aZ;
        pPlayer->MoveTo(pCell, pos);
    }
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
    Breadcrumbs::Record("SetPlayerGodMode", static_cast<uint32_t>(aEnable));
    PlayerCharacter::SetGodMode(aEnable);
}

void SkyrimBridge::SetPlayerRespawnMode(bool aEnable) noexcept
{
    Breadcrumbs::Record("SetPlayerRespawnMode", static_cast<uint32_t>(aEnable));
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->SetPlayerRespawnMode(aEnable);
}

void SkyrimBridge::RespawnPlayer() noexcept
{
    Breadcrumbs::Record("RespawnPlayer", 0);
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->RespawnPlayer();
}

void SkyrimBridge::KnockdownPlayer(float aForce) noexcept
{
    Breadcrumbs::Record("KnockdownPlayer", 0);
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
    Breadcrumbs::Record("PlayerPayGold", aAmount);
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->PayGold(static_cast<int32_t>(aAmount));
}

void SkyrimBridge::PlayerPayCrimeGoldToAllFactions() noexcept
{
    Breadcrumbs::Record("PlayerPayCrimeGoldToAllFactions", 0);
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        pPlayer->PayCrimeGoldToAllFactions();
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

void SkyrimBridge::StopTeleportEvents() noexcept
{
    PlayerCharacter::StopTeleportEvents();
}

void SkyrimBridge::ShowHudMessage(const std::string& aMessage) noexcept
{
    Utils::ShowHudMessage(String(aMessage));
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
    Breadcrumbs::Record("ForceWeather", aWeatherFormId, static_cast<uint32_t>(aOverride));
    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(aWeatherFormId));
    if (!pWeather)
    {
        spdlog::warn("SkyrimBridge::ForceWeather — weather {:X} not found", aWeatherFormId);
        return;
    }
    if (Sky* pSky = Sky::Get())
        pSky->ForceWeather(pWeather);
}

void SkyrimBridge::ReleaseWeatherOverride() noexcept
{
    Breadcrumbs::Record("ReleaseWeatherOverride", 0);
    if (Sky* pSky = Sky::Get())
        pSky->ReleaseWeatherOverride();
}

std::string SkyrimBridge::GetModName(uint32_t aIndex) const noexcept
{
    // TODO(02.5-02): implement via Sky/ModManager
    return {};
}

void SkyrimBridge::SetGameWeatherSystemEnabled(bool aEnable) noexcept
{
    Breadcrumbs::Record("SetGameWeatherSystemEnabled", static_cast<uint32_t>(aEnable));
    // TODO(02.5-02): implement via Sky toggle
    spdlog::warn("SkyrimBridge::SetGameWeatherSystemEnabled({}) not yet implemented", aEnable);
}

float SkyrimBridge::GetGameTimeHours() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameHour ? pTime->GameHour->f : 0.f;
    return 0.f;
}

void SkyrimBridge::SetGameTimeHours(float aHours) noexcept
{
    Breadcrumbs::Record("SetGameTimeHours", 0);
    // TODO(02.5-02): implement
    spdlog::warn("SkyrimBridge::SetGameTimeHours({}) not yet implemented", aHours);
}

float SkyrimBridge::GetGameTimeScale() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->TimeScale ? pTime->TimeScale->f : 0.f;
    return 0.f;
}

float SkyrimBridge::GetGameYear() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameYear ? pTime->GameYear->f : 0.f;
    return 0.f;
}

float SkyrimBridge::GetGameMonth() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameMonth ? pTime->GameMonth->f : 0.f;
    return 0.f;
}

float SkyrimBridge::GetGameDay() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameDay ? pTime->GameDay->f : 0.f;
    return 0.f;
}

// ---------------------------------------------------------------------------
// IActorBridge — extended (Plan 03)
// ---------------------------------------------------------------------------

ActorValues SkyrimBridge::GetActorEssentialValues(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    return pActor->GetEssentialActorValues();
}

void SkyrimBridge::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    Breadcrumbs::Record("SetActorValues", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetActorValues(acValues);
}

Inventory SkyrimBridge::GetActorInventory(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    return pActor->GetActorInventory();
}

void SkyrimBridge::SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept
{
    Breadcrumbs::Record("SetActorInventory", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetActorInventory(acInventory);
}

Factions SkyrimBridge::GetActorFactions(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    return pActor->GetFactions();
}

void SkyrimBridge::SetActorFactions(uint32_t aFormId, const Factions& acFactions) noexcept
{
    Breadcrumbs::Record("SetActorFactions", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetFactions(acFactions);
}

void SkyrimBridge::SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept
{
    Breadcrumbs::Record("SetActorWeaponDrawnEx", aFormId, static_cast<uint32_t>(aDraw));
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetWeaponDrawnEx(aDraw);
}

WeaponState SkyrimBridge::GetWeaponState(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return WeaponState::Sheathed;
    return static_cast<WeaponState>(pActor->actorState.GetWeaponState());
}

bool SkyrimBridge::IsActorTemporary(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    return pActor->IsTemporary();
}

bool SkyrimBridge::IsActorMount(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    return pActor->IsMount();
}

bool SkyrimBridge::IsActorDragon(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    return pActor->IsDragon();
}

bool SkyrimBridge::IsActorVampireLord(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    return pActor->IsVampireLord();
}

bool SkyrimBridge::IsActorDisabled(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    return pActor->IsDisabled();
}

void SkyrimBridge::EnableActor(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("EnableActor", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->EnableImpl();
}

void SkyrimBridge::ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept
{
    Breadcrumbs::Record("ActorSpeakSound", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SpeakSound(acFilename.c_str());
}

void SkyrimBridge::ActorStopCurrentDialogue(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("ActorStopCurrentDialogue", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->StopCurrentDialogue(true);
}

void SkyrimBridge::SetActorIgnoreFriendlyHit(uint32_t aFormId, bool aIgnore) noexcept
{
    Breadcrumbs::Record("SetActorIgnoreFriendlyHit", aFormId, static_cast<uint32_t>(aIgnore));
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetIgnoreFriendlyHit(aIgnore);
}

void SkyrimBridge::SetActorPlayerRespawnMode(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("SetActorPlayerRespawnMode", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetPlayerRespawnMode();
}

void SkyrimBridge::SetActorCommandingActor(uint32_t aFormId, uint32_t aCommanderFormId) noexcept
{
    Breadcrumbs::Record("SetActorCommandingActor", aFormId, aCommanderFormId);
    Actor* pActor = GetActor(aFormId);
    Actor* pCommander = GetActor(aCommanderFormId);
    if (pActor && pCommander)
        pActor->SetCommandingActor(pCommander->GetHandle());
}

uint32_t SkyrimBridge::GetActorCommandingActorFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return 0;
    auto handle = pActor->GetCommandingActor();
    if (!handle)
        return 0;
    return handle->formID;
}

void SkyrimBridge::SetActorPackage(uint32_t aFormId, uint32_t aPackageFormId) noexcept
{
    Breadcrumbs::Record("SetActorPackage", aFormId, aPackageFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    TESPackage* pPkg = Cast<TESPackage>(TESForm::GetById(aPackageFormId));
    if (pPkg)
        pActor->SetPackage(pPkg);
}

void SkyrimBridge::ActorLoadAnimationVariables(uint32_t aFormId, const AnimationVariables& acVars) noexcept
{
    Breadcrumbs::Record("ActorLoadAnimationVariables", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->LoadAnimationVariables(acVars);
}

void SkyrimBridge::ActorSaveAnimationVariables(uint32_t aFormId, AnimationVariables& aVars) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SaveAnimationVariables(aVars);
}

ActionEvent SkyrimBridge::GetActorLatestAnimation(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    return pActor->GetExtension()->LatestAnimation;
}

void SkyrimBridge::SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acEvent) noexcept
{
    Breadcrumbs::Record("SetActorLatestAnimation", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->GetExtension()->LatestAnimation = acEvent;
}

void SkyrimBridge::ActorFixVampireLordModel(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("ActorFixVampireLordModel", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->FixVampireLordModel();
}

void SkyrimBridge::ActorInitiateMountPackage(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept
{
    Breadcrumbs::Record("ActorInitiateMountPackage", aRiderFormId, aMountFormId);
    Actor* pRider = GetActor(aRiderFormId);
    Actor* pMount = GetActor(aMountFormId);
    if (pRider && pMount)
        pRider->InitiateMountPackage(pMount);
}

uint32_t SkyrimBridge::GetActorParentCellFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->parentCell)
        return 0;
    return pActor->parentCell->formID;
}

uint32_t SkyrimBridge::GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return 0;
    auto* pWS = pActor->GetWorldSpace();
    return pWS ? pWS->formID : 0;
}

uint32_t SkyrimBridge::SpawnActorFromNpc(uint32_t aNpcFormId) noexcept
{
    Breadcrumbs::Record("SpawnActorFromNpc", aNpcFormId);
    TESNPC* pNpc = Cast<TESNPC>(TESForm::GetById(aNpcFormId));
    if (!pNpc)
        return 0;
    Actor* pActor = Actor::Create(pNpc);
    return pActor ? pActor->formID : 0;
}

uint32_t SkyrimBridge::CreateCustomActor(const TiltedPhoques::String& acAppearance, uint32_t aChangeFlags) noexcept
{
    Breadcrumbs::Record("CreateCustomActor", aChangeFlags);
    TESNPC* pNpc = TESNPC::Create(acAppearance, aChangeFlags);
    if (!pNpc)
        return 0;
    Actor* pActor = Actor::Create(pNpc);
    return pActor ? pActor->formID : 0;
}

void SkyrimBridge::DeserializeActorNpc(uint32_t aFormId, const TiltedPhoques::String& acAppearance, uint32_t aChangeFlags) noexcept
{
    Breadcrumbs::Record("DeserializeActorNpc", aFormId, aChangeFlags);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    if (!pNpc)
        return;
    pNpc->Deserialize(acAppearance, aChangeFlags);
}

uint32_t SkyrimBridge::SerializeActorNpc(uint32_t aFormId, TiltedPhoques::String& aBuffer) noexcept
{
    Breadcrumbs::Record("SerializeActorNpc", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return 0;
    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    if (!pNpc)
        return 0;
    pNpc->Serialize(&aBuffer);
    return pNpc->GetChangeFlags();
}

void SkyrimBridge::MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept
{
    Breadcrumbs::Record("MarkActorNpcChanged", aFormId, aFlags);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    if (pNpc)
        pNpc->MarkChanged(aFlags);
}

uint32_t SkyrimBridge::GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return 0;
    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    if (!pNpc)
        return 0;
    if (!pNpc->IsTemporary())
        return 0;
    TESNPC* pTemplate = pNpc->GetTemplateBase();
    return pTemplate ? pTemplate->formID : 0;
}

bool SkyrimBridge::IsActorNpcTemporary(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    return pNpc ? pNpc->IsTemporary() : false;
}

Tints SkyrimBridge::GetActorFaceTints(uint32_t aFormId) const noexcept
{
    // Player-specific: only player has face tints in Skyrim
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || pPlayer->formID != aFormId)
        return {};

    Tints result;
    const auto& tints = pPlayer->GetTints();
    result.Entries.resize(tints.length);
    for (auto i = 0u; i < tints.length; ++i)
    {
        result.Entries[i].Alpha = tints[i]->alpha;
        result.Entries[i].Color = tints[i]->color;
        result.Entries[i].Type = tints[i]->type;
        if (tints[i]->texture)
            result.Entries[i].Name = tints[i]->texture->name.AsAscii();
    }
    return result;
}

void SkyrimBridge::SetupActorFaceGen(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("SetupActorFaceGen", aFormId);
    // FaceGen setup is entity-based; this is a no-op at bridge level
    // Actual setup happens in CharacterService::OnCharacterSpawn via FaceGenSystem::Setup
}

void SkyrimBridge::SetupActorCache(uint32_t aFormId, uint32_t aEntityHint) noexcept
{
    Breadcrumbs::Record("SetupActorCache", aFormId, aEntityHint);
    // aEntityHint is the entity (CharacterService::ProcessNewEntity), as before the bridge refactor.
    if (Actor* pActor = GetActor(aFormId))
        CacheSystem::Setup(World::Get(), static_cast<entt::entity>(aEntityHint), pActor);
}

void SkyrimBridge::ActorUpdateInterpolation(uint32_t aFormId, uint64_t aTick) noexcept
{
    Breadcrumbs::Record("ActorUpdateInterpolation", aFormId);
    // InterpolationSystem::Update requires InterpolationComponent; called directly in CharacterService
}

// These two were empty since the bridge refactor, which silently disabled animation replay
// (remote actors never drew weapons, attacked, ...) and remote face tints.
void SkyrimBridge::ActorUpdateAnimation(uint32_t aFormId, uint64_t aTick) noexcept
{
    Breadcrumbs::Record("ActorUpdateAnimation", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    auto& world = World::Get();
    auto view = world.view<FormIdComponent, RemoteAnimationComponent>();
    for (auto entity : view)
        if (view.get<FormIdComponent>(entity).Id == aFormId)
            AnimationSystem::Update(world, pActor, view.get<RemoteAnimationComponent>(entity), aTick);
}

void SkyrimBridge::ActorUpdateFaceGen(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("ActorUpdateFaceGen", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    auto& world = World::Get();
    auto view = world.view<FormIdComponent, FaceGenComponent>();
    for (auto entity : view)
        if (view.get<FormIdComponent>(entity).Id == aFormId)
            FaceGenSystem::Update(world, pActor, view.get<FaceGenComponent>(entity));
}

void SkyrimBridge::ApplyRevealEffect(uint32_t aFormId, float aMagnitude) noexcept
{
    Breadcrumbs::Record("ApplyRevealEffect", aFormId);
    // Apply SkyrimTogether.esp detect effect to a remote player
    Mod* pSkyrimTogether = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!pSkyrimTogether)
        return;
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById((pSkyrimTogether->standardId << 24) | 0x1825));
    if (!pSpell)
        return;

    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;

    MagicTarget::AddTargetData data{};
    data.pSpell = pSpell;
    data.pEffectItem = pSpell->GetEffect((pSkyrimTogether->standardId << 24) | 0x1824);
    data.fMagnitude = aMagnitude;
    data.fUnkFloat1 = 1.f;
    data.eCastingSource = MagicSystem::CastingSource::CASTING_SOURCE_COUNT;

    pActor->magicTarget.AddTarget(data, false, false);
}

namespace
{
// EffectSetting (1.6.1170, CommonLibSSE): EffectSettingData at +0x68; flags +0x00, effectShader
// (the hit shader) +0x30, hitEffectArt +0x88.
struct HitVisuals
{
    TESForm* pShader;
    TESForm* pArt;
};

HitVisuals GetHitVisuals(uint32_t aEffectFormId) noexcept
{
    constexpr uint32_t cNoHitEffect = 1u << 27;
    const auto* pEffect = Cast<EffectSetting>(TESForm::GetById(aEffectFormId));
    if (!pEffect)
        return {};
    const auto* pData = reinterpret_cast<const uint8_t*>(pEffect) + 0x68;
    if (*reinterpret_cast<const uint32_t*>(pData) & cNoHitEffect)
        return {};
    return {*reinterpret_cast<TESForm* const*>(pData + 0x30), *reinterpret_cast<TESForm* const*>(pData + 0x88)};
}


} // namespace

bool SkyrimBridge::EffectHasHitVisuals(uint32_t aEffectFormId) const noexcept
{
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId);
    return cVisuals.pShader || cVisuals.pArt;
}

bool SkyrimBridge::PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept
{
    Breadcrumbs::Record("PlayEffectHitVisuals", aFormId, aEffectFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetNiNode())
        return false;
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId);
    // TESObjectREFR::ApplyEffectShader / ApplyArtObject (shader or art, duration (-1: until
    // stopped), facing ref, face target, attach to camera, attach node, interface effect), as
    // Papyrus Play and the console's PlayMagicShaderVisuals do.
    TP_THIS_FUNCTION(TApply, bool, TESObjectREFR, TESForm*, float, TESObjectREFR*, bool, bool, void*, bool);
    POINTER_SKYRIMSE(TApply, s_applyEffectShader, 19872);
    POINTER_SKYRIMSE(TApply, s_applyArtObject, 22769);
    if (cVisuals.pShader)
        TiltedPhoques::ThisCall(s_applyEffectShader.Get(), pActor, cVisuals.pShader, -1.f, nullptr, false, false, nullptr, false);
    if (cVisuals.pArt)
        TiltedPhoques::ThisCall(s_applyArtObject.Get(), pActor, cVisuals.pArt, -1.f, nullptr, false, false, nullptr, false);
    return IsPlayingEffectHitVisuals(aFormId, aEffectFormId);
}

bool SkyrimBridge::IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept
{
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId);
    Actor* pActor = GetActor(aFormId);
    ProcessLists* pProcessLists = ProcessLists::Get();
    const auto cPlaying = pActor && pProcessLists ? pProcessLists->GetPlayingVisualFormIds(pActor->GetHandle().handle.iBits)
                                                  : std::vector<uint32_t>{};
    const auto cIsPlaying = [&](const TESForm* apForm) {
        return !apForm || std::binary_search(cPlaying.begin(), cPlaying.end(), apForm->formID);
    };
    return (cVisuals.pShader || cVisuals.pArt) && cIsPlaying(cVisuals.pShader) && cIsPlaying(cVisuals.pArt);
}

void SkyrimBridge::StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept
{
    Breadcrumbs::Record("StopEffectHitVisuals", aFormId, aEffectFormId);
    Actor* pActor = GetActor(aFormId);
    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pActor || !pProcessLists)
        return;
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId);
    if (cVisuals.pShader)
        pProcessLists->StopEffectShader(pActor, cVisuals.pShader);
    if (cVisuals.pArt)
        pProcessLists->StopArtObject(pActor, cVisuals.pArt);
}

void SkyrimBridge::PlayerKnockExplosion(uint32_t aFormId, float aX, float aY, float aZ, float aForce) noexcept
{
    Breadcrumbs::Record("PlayerKnockExplosion", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    NiPoint3 pos;
    pos.x = aX;
    pos.y = aY;
    pos.z = aZ;
    pActor->currentProcess->KnockExplosion(pActor, &pos, aForce);
}

// ---------------------------------------------------------------------------
// IFormBridge — extended (Plan 03)
// ---------------------------------------------------------------------------

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

    // Set werewolf/vampire lord graph descriptor
    if (pEffect->IsWerewolfEffect())
        pTarget->GetExtension()->GraphDescriptorHash = AnimationGraphDescriptor_WerewolfBehavior::m_key;
    if (pEffect->IsVampireLordEffect())
        pTarget->GetExtension()->GraphDescriptorHash = AnimationGraphDescriptor_VampireLordBehavior::m_key;

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

// ---------------------------------------------------------------------------
// IPlayerBridge — extended (Plan 03)
// ---------------------------------------------------------------------------

QuestLog SkyrimBridge::GetPlayerQuestLog() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return {};

    QuestLog result;
    auto& modSystem = World::Get().GetModSystem();

    for (const auto& objective : pPlayer->objectives)
    {
        auto* pQuest = objective.instance->quest;
        if (!pQuest)
            continue;
        if (!QuestService::IsNonSyncableQuest(pQuest->formID, *this))
        {
            GameId id{};
            if (modSystem.GetServerModId(pQuest->formID, id))
            {
                auto& entry = result.Entries.emplace_back();
                entry.Stage = pQuest->currentStage;
                entry.Id = id;
            }
        }
    }
    // remove duplicates
    const auto ip = std::unique(result.Entries.begin(), result.Entries.end());
    result.Entries.resize(std::distance(result.Entries.begin(), ip));
    return result;
}

Tints SkyrimBridge::GetPlayerFaceTints() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return {};
    return GetActorFaceTints(pPlayer->formID);
}

void SkyrimBridge::AddPlayerSkillExperience(int32_t aSkillId, float aExperience) noexcept
{
    Breadcrumbs::Record("AddPlayerSkillExperience", static_cast<uint32_t>(aSkillId));
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer)
        pPlayer->AddSkillExperience(aSkillId, aExperience);
}

int32_t SkyrimBridge::GetPlayerLastUsedCombatSkill() const noexcept
{
    return PlayerCharacter::LastUsedCombatSkill;
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

bool SkyrimBridge::IsLoadDoor(uint32_t aObjectFormId) const noexcept
{
    const TESObjectREFR* pObject = GetRefr(aObjectFormId);
    return pObject && pObject->baseForm && pObject->baseForm->formType == FormType::Door &&
           pObject->extraData.Contains(ExtraDataType::Teleport);
}
