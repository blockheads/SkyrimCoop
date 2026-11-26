// SkyrimBridge: an actor's values, state and AI (IActorState).
#include "SkyrimBridgeInternal.h"

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

float SkyrimBridge::GetActorTemporaryValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->GetActorTemporaryValue(aIndex);
    return 0.f;
}

float SkyrimBridge::GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return EngineVirtual(pActor->actorValueOwner).GetBaseValue(static_cast<uint32_t>(aIndex));
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

void SkyrimBridge::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    Breadcrumbs::Record("SetActorValues", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetActorValues(acValues);
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

ActorStateFields SkyrimBridge::GetActorStateFields(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    ActorStateFields fields;
    fields.Flags1 = pActor->actorState.flags1;
    fields.Flags2 = pActor->actorState.flags2;
    fields.InKillMove = pActor->IsInKillMove();
    if (fields.InKillMove)
        if (const TESObjectREFR* pKiller = TESObjectREFR::GetByHandle(pActor->killerHandle))
            fields.KillerFormId = pKiller->formID;
    fields.InPair = fields.InKillMove && (pActor->InPair() || pActor->InPairClip());
    fields.Dead = pActor->IsDead();
    fields.Noise = pActor->iActionValue;
    return fields;
}

void SkyrimBridge::SetCopyActorStateFlags(uint32_t aFormId, uint32_t aFlags1, uint32_t aFlags2) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetExtension()->IsCopy())
        return;
    pActor->actorState.flags1 = aFlags1;
    pActor->actorState.flags2 = aFlags2;
}

void SkyrimBridge::SetCopyNoise(uint32_t aFormId, int32_t aNoise) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetExtension()->IsCopy())
        return;
    pActor->iActionValue = aNoise;
    pActor->fTimerOnAction = ActorStateRules::CopyNoiseTimer(aNoise);
}

ActorAiFields SkyrimBridge::GetActorAiFields(uint32_t aFormId) const noexcept
{
    ActorAiFields fields;
    const Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->currentProcess)
        return fields;
    if (const TESPackage* pPackage = pActor->GetCurrentPackage())
        fields.PackageId = pPackage->formID;
    if (const BGSScene* pScene = pActor->GetOwnCurrentScene())
        fields.SceneId = pScene->formID;
    fields.CombatState = pActor->GetCombatState();
    if (const TESObjectREFR* pTarget = TESObjectREFR::GetByHandle(pActor->combatHandle))
        fields.CombatTargetId = pTarget->formID;
    if (const HighProcessData* pHigh = pActor->currentProcess->highProcess)
        fields.HeadTrackIds = pHigh->GetHeadTrackTargets();
    if (const TESObjectREFR* pFurniture = pActor->GetSeatFurniture())
        fields.FurnitureId = pFurniture->formID;
    return fields;
}

Perks SkyrimBridge::GetPlayerPerks() const noexcept
{
    Perks perks;
    if (const auto* pPlayer = PlayerCharacter::Get())
        for (const PlayerCharacter::PerkRank* pRank : pPlayer->perkRanks)
            if (pRank && pRank->perk && pRank->rank)
                if (const auto cId = ToSession(pRank->perk->formID))
                {
                    auto it = std::ranges::find(perks.Ranks, *cId, &PlayerRecord::Perk::Id);
                    if (it == perks.Ranks.end())
                        perks.Ranks.push_back({*cId, pRank->rank});
                    else
                        it->Rank = std::max(it->Rank, pRank->rank);
                }
    std::ranges::sort(perks.Ranks, {}, [](const auto& acRank) { return std::pair(acRank.Id.ModId, acRank.Id.BaseId); });
    if (auto* pPlayer = PlayerCharacter::Get())
        for (uint32_t i = 0; i < pPlayer->addedSpells.size; ++i)
            if (const SpellItem* cpSpell = pPlayer->addedSpells.Get(i))
                if (const auto cId = ToSession(cpSpell->formID))
                    perks.Spells.push_back(*cId);
    return perks;
}

void SkyrimBridge::SetCopyPerks(uint32_t aFormId, const Perks& acPerks) noexcept
{
    MainLoopTasks::Queue([aFormId, acPerks] {
        Actor* pActor = GetActor(aFormId);
        if (!pActor || !pActor->GetExtension()->IsOtherPlayer())
            return;
        // The owner's added spells, for HasSpell: written, not AddSpell'd (38716 casts an ability, the owner's to apply).
        std::vector<SpellItem*> spells;
        for (const GameId& cId : acPerks.Spells)
            if (auto* pSpell = Cast<SpellItem>(TESForm::GetById(ToGame(cId))))
                spells.push_back(pSpell);
        pActor->addedSpells.Assign(spells);
        auto* pBase = Cast<TESNPC>(pActor->baseForm);
        if (!pBase || !pBase->IsTemporary())
            return;
        std::vector<PerkRankData> ranks;
        for (const auto& cRank : acPerks.Ranks)
            if (auto* pPerk = Cast<BGSPerk>(TESForm::GetById(ToGame(cRank.Id))); pPerk && cRank.Rank)
                ranks.push_back({pPerk, cRank.Rank});
        // TESNPC::CopyPerkRankArray (CommonLib): replace the copy's unique base, between engine frames.
        auto* pRanks = ranks.empty() ? nullptr : static_cast<PerkRankData*>(Memory::Allocate(ranks.size() * sizeof(PerkRankData)));
        if (pRanks)
            std::ranges::copy(ranks, pRanks);
        Memory::Free(pBase->perkRanks.perkRanks);
        pBase->perkRanks.perkRanks = pRanks;
        pBase->perkRanks.count = static_cast<uint32_t>(ranks.size());
    });
}

void SkyrimBridge::SetCopyAiFields(uint32_t aFormId, const ActorAiFields& acFields) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetExtension()->IsCopy())
        return;
    pActor->GetExtension()->OwnerPackageId = acFields.PackageId;
    pActor->GetExtension()->OwnerSceneId = acFields.SceneId;
    // Put in its slots before the engine reads them (AiStateEvents' ProcessTracking hook): a copy runs no AI to set them.
    for (size_t i = 0; i < acFields.HeadTrackIds.size(); ++i)
        pActor->GetExtension()->HeadTrack[i].store(acFields.HeadTrackIds[i], std::memory_order_relaxed);
    pActor->LinkFurnitureGraphs(acFields.FurnitureId ? Cast<TESObjectREFR>(TESForm::GetById(acFields.FurnitureId)) : nullptr);
    if (!pActor->currentProcess)
        return;
    // The owner's package in the running slot, read by conditions, Papyrus and plugins as for any NPC. No target: the
    // engine's Disable cleanup acts on a package aimed at another actor (actor-ai-and-copies.md).
    const ActorPackage& cRunning = pActor->currentProcess->currentPackage;
    auto* pPackage = acFields.PackageId ? Cast<TESPackage>(TESForm::GetById(acFields.PackageId)) : nullptr;
    if (pPackage && cRunning.package != pPackage)
    {
        Breadcrumbs::Record("PutOwnerPackage", aFormId, acFields.PackageId);
        pActor->PutOwnerPackage(pPackage);
    }
    else if (!pPackage)
        pActor->InitiateDoNothingPackage();
}
