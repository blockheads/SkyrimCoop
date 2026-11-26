// SkyrimBridge: hits, combat and death (IActorCombat).
#include "SkyrimBridgeInternal.h"

bool SkyrimBridge::DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept
{
    Breadcrumbs::Record("DamageActorHealth", aFormId, aAttackerFormId);
    Actor* pActor = GetActor(aFormId);
    // The kill move decides its victim's death: a hit now would only queue a kill credited to the local player
    // (EndDeferredKill 38618), not to the kill move's partner.
    if (!pActor || pActor->IsHeldByKillMove())
        return false;
    pActor->DamageHealth(aDamage, aAttackerFormId ? GetActor(aAttackerFormId) : nullptr);
    return true;
}

uint32_t SkyrimBridge::GetCombatTargetFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->pCombatController)
        return 0;
    Actor* pTarget = pActor->GetCombatTarget();
    return pTarget ? pTarget->formID : 0;
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
    if (pAttacker->GetExtension()->IsOtherPlayer())
    {
        using TGetFactionReaction = int32_t(Actor*, Actor*);
        POINTER_SKYRIMSE(TGetFactionReaction, s_getFactionReaction, 37666);
        const int32_t cReaction = (s_getFactionReaction.Get())(pVictim, pAttacker);
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
            static std::unordered_map<uint64_t, Hits> s_friendlyHits;
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
    using TGetAggression = uint32_t(Actor*);
    POINTER_SKYRIMSE(TGetAggression, s_getAggression, 37671);
    using TAttacked = void(Actor*, Actor*, uint64_t, uint32_t);
    POINTER_SKYRIMSE(TAttacked, s_attacked, 38626);

    const uint32_t cAggression = (s_getAggression.Get())(pVictim);
    s_attacked.Get()(pVictim, pAttacker, 0, cAggression);
    spdlog::debug("ReactToAttack: {:X} attacked by {:X} (aggression {})", aVictimFormId, aAttackerFormId, cAggression);
}

void SkyrimBridge::CopyLeaveKillMove(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetExtension()->IsCopy())
        return;
    Breadcrumbs::Record("CopyLeaveKillMove", aFormId);
    pActor->killerHandle = *TESObjectREFR::GetNullHandle();
    pActor->EndKillMove();
}

void SkyrimBridge::ActorHoldKill(uint32_t aFormId, bool aHold) noexcept
{
    Breadcrumbs::Record("ActorHoldKill", aFormId, static_cast<uint32_t>(aHold));
    if (Actor* pActor = GetActor(aFormId))
        pActor->HoldKill(aHold);
}

bool SkyrimBridge::ActorKillCounted(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->KillCounted();
}

bool SkyrimBridge::ActorKillQueued(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->KillQueued();
}

void SkyrimBridge::KillActorBy(uint32_t aFormId, uint32_t aKillerFormId) noexcept
{
    Breadcrumbs::Record("KillActorBy", aFormId, aKillerFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->KillBy(aKillerFormId ? GetActor(aKillerFormId) : nullptr);
}

void SkyrimBridge::ActorYieldPair(uint32_t aOwnFormId, uint32_t aCopyFormId) noexcept
{
    Breadcrumbs::Record("ActorYieldPair", aOwnFormId, aCopyFormId);
    Actor* pOwn = GetActor(aOwnFormId);
    Actor* pCopy = GetActor(aCopyFormId);
    if (pOwn && pCopy)
        AnimationBody::YieldPair(World::Get(), pCopy, pOwn);
}

bool SkyrimBridge::ActorPerformPlainly(uint32_t aFormId, uint32_t aActionId) noexcept
{
    Breadcrumbs::Record("ActorPerformPlainly", aFormId, aActionId);
    Actor* pActor = GetActor(aFormId);
    return pActor && pActor->PerformPlainly(Cast<BGSAction>(TESForm::GetById(aActionId)));
}

bool SkyrimBridge::ActorCanPair(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && !pActor->IsDead() && (pActor->flags1 & Actor::kProcessMe) != 0 && !pActor->IsPlayerSummon();
}

void SkyrimBridge::CopyEnterKillMove(uint32_t aFormId, uint32_t aKillerFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetExtension()->IsCopy())
        return;
    Breadcrumbs::Record("CopyEnterKillMove", aFormId, aKillerFormId);
    pActor->EnterKillMove(aKillerFormId ? GetActor(aKillerFormId) : nullptr);
}

void SkyrimBridge::TakeTrapHit(uint32_t aFormId, uint32_t aTrapFormId, const Payloads::TrapHit& acHit) noexcept
{
    Breadcrumbs::Record("TakeTrapHit", aFormId, aTrapFormId);
    if (Actor* pActor = GetActor(aFormId); pActor && !pActor->GetExtension()->IsCopy())
        Traps::TakeHit(*pActor, Cast<TESObjectREFR>(TESForm::GetById(aTrapFormId)), acHit);
}

void SkyrimBridge::StaggerActor(uint32_t aFormId, float aMagnitude, uint32_t aHitterId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || pActor->GetExtension()->IsCopy())
        return;
    // Actor::Stagger (37710): native perks, direction and the stagger action, without applying damage again.
    using TStagger = void(Actor*, float, Actor*);
    POINTER_SKYRIMSE(TStagger, s_stagger, 37710);
    s_stagger(pActor, aMagnitude, GetActor(aHitterId));
}

void SkyrimBridge::SetCopyCombat(uint32_t aFormId, const CopyCombatFields& acFields) noexcept
{
    if (Actor* pActor = GetActor(aFormId); pActor && pActor->GetExtension()->IsCopy())
        CopyCombatGroups::Hold(*pActor, acFields);
}

CombatGroupRead SkyrimBridge::GetCombatGroup(uint32_t aFormId) const noexcept
{
    CombatGroupRead read;
    const Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return read;
    // The player fights with no controller: its own group holds its enemies (41261).
    CombatGroup* pGroup = pActor == PlayerCharacter::Get() ? PlayerCharacter::Get()->combatGroup
                          : pActor->pCombatController ? pActor->pCombatController->pCombatGroup
                                                      : nullptr;
    if (!pGroup)
        return read;
    BSReadLockGuard lock(pGroup->lock);
    read.Exists = true;
    read.Id = pGroup->groupID;
    for (const CombatTarget& cTarget : pGroup->targets)
        if (const TESObjectREFR* pTarget = TESObjectREFR::GetByHandle(cTarget.targetHandle))
            read.Targets.push_back(pTarget->formID);
    return read;
}
