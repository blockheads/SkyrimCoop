// SkyrimBridge: who an actor is (IActorIdentity).
#include "SkyrimBridgeInternal.h"

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

bool SkyrimBridge::IsOtherPlayer(uint32_t aFormId) const noexcept
{
    const ActorExtension* pExt = GetExtension(aFormId);
    return pExt && pExt->IsOtherPlayer();
}

void SkyrimBridge::SetActorCopy(uint32_t aFormId, bool aCopy) noexcept
{
    Breadcrumbs::Record("SetActorCopy", aFormId, static_cast<uint32_t>(aCopy));
    if (ActorExtension* pExt = GetExtension(aFormId))
    {
        const bool cReleased = pExt->IsCopy() && !aCopy;
        pExt->SetCopy(aCopy);
        if (!aCopy)
            pExt->ClaimedIn.store(GuestNpcs::Session()); // this game's own from now: not held (GuestNpcs)
        GuestScripts::SetGuest(aFormId, pExt->IsOtherPlayer()); // GuestScripts' list follows the flags
        if (Actor* pActor = GetActor(aFormId))
        {
            if (cReleased)
                pActor->EndReleasedCopyPackage();
            if (cReleased && ActorStateRules::HoldsOwnersNoise(pActor->fTimerOnAction))
                pActor->iActionValue = 0, pActor->fTimerOnAction = 0.f;
            pActor->ApplyHeld3D(); // a copy or claimed: its refused 3D loads
        }
    }
}

void SkyrimBridge::SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept
{
    Breadcrumbs::Record("SetActorPlayer", aFormId, static_cast<uint32_t>(aPlayer));
    if (ActorExtension* pExt = GetExtension(aFormId))
    {
        pExt->SetPlayer(aPlayer);
        GuestScripts::SetGuest(aFormId, pExt->IsOtherPlayer()); // GuestScripts' list follows the flags
    }
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

bool SkyrimBridge::SetActorPersistent(uint32_t aFormId, bool aPersistent) noexcept
{
    Breadcrumbs::Record("SetActorPersistent", aFormId, aPersistent);
    if (Actor* pActor = GetActor(aFormId))
    {
        const bool cWasPersistent = (pActor->flags & TESForm::PERSISTENT) != 0;
        // 19597 updates the flag and the world's persistent-cell membership together.
        if (cWasPersistent != aPersistent)
        {
            using TSetPersistent = uintptr_t(TESObjectREFR*, bool);
            POINTER_SKYRIMSE(TSetPersistent, s_setPersistent, 19597);
            s_setPersistent(pActor, aPersistent);
        }
        return cWasPersistent;
    }
    return false;
}

void SkyrimBridge::KillActor(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("KillActor", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->Kill();
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

void SkyrimBridge::ResumeActorAnimation(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || pActor->IsDead())
        return;
    const uint32_t cHandle = pActor->GetHandle().handle.iBits;
    MainLoopTasks::Queue([cHandle] { ResumeCopyGraph(cHandle, 0); });
}

void SkyrimBridge::DeleteActor(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("DeleteActor", aFormId);
    // Engine Delete is for dynamically-created references only. Never delete
    // the local player or an NPC reference that belongs to the save/plugin.
    if ((aFormId & 0xFF000000u) != 0xFF000000u) return;
    GuestScripts::SetGuest(aFormId, false);
    if (auto* actor = GetActor(aFormId))
        actor->Delete();
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

uint32_t SkyrimBridge::GetActorBaseFormId(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->baseForm ? pActor->baseForm->formID : 0;
    return 0;
}

bool SkyrimBridge::SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept
{
    Breadcrumbs::Record("SetActorBaseFormId", aFormId, aBaseFormId);
    Actor* pActor = GetActor(aFormId);
    TESNPC* pNpc = Cast<TESNPC>(TESForm::GetById(aBaseFormId));
    if (!pActor || !pNpc)
        return false;
    if (pActor->baseForm != pNpc)
    {
        pActor->GetExtension()->GraphDescriptorHash = 0;
        pActor->SetBaseObject(pNpc);
    }
    return true;
}

std::string SkyrimBridge::GetActorDisplayName(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->GetDisplayFullName();
    return {};
}

void SkyrimBridge::SetActorDisplayName(uint32_t aFormId, const std::string& acName) noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        pActor->extraData.SetCustomName(acName.c_str(), true);
        spdlog::debug("Actor {:X} named '{}'", aFormId, acName);
    }
}

void SkyrimBridge::SetActorBaseLevel(uint32_t aFormId, uint16_t aLevel) noexcept
{
    if (Actor* pActor = GetActor(aFormId); pActor && pActor->GetExtension()->IsCopy())
        if (auto* pNpc = Cast<TESNPC>(pActor->baseForm))
        {
            pNpc->actorData.level = aLevel;
            pNpc->MarkChanged(0x2); // TESActorBaseData::SetLevel (14385): CHANGE_ACTOR_BASE_DATA
        }
}

float SkyrimBridge::GetActorBaseWeight(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    const TESNPC* pNpc = pActor ? Cast<TESNPC>(pActor->baseForm) : nullptr;
    return pNpc ? pNpc->weight : 50.f;
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

bool SkyrimBridge::IsActorCopy(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor && pActor->GetExtension()->IsCopy();
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

bool SkyrimBridge::IsActorStillLoading(uint32_t aFormId) const noexcept
{
    const auto* pActor = GetActor(aFormId);
    return pActor && pActor->IsStillLoading();
}

bool SkyrimBridge::IsActorDisabled(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return false;
    return pActor->IsDisabled();
}

bool SkyrimBridge::IsActorDeleted(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor && pActor->IsDeleted();
}

void SkyrimBridge::EnableActor(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("EnableActor", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->EnableImpl();
}

void SkyrimBridge::SetActorHidden(uint32_t aFormId, bool aHidden) noexcept
{
    Breadcrumbs::Record("SetActorHidden", aFormId, static_cast<uint32_t>(aHidden));
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    // HookEnable and HookSetDisabled read it.
    HiddenCopies::Set(aFormId, aHidden);
    if (aHidden && !pActor->IsDisabled())
        pActor->DisableImpl();
}

void SkyrimBridge::DisableActor(uint32_t aFormId, bool aDelete) noexcept
{
    Breadcrumbs::Record("DisableActor", aFormId, static_cast<uint32_t>(aDelete));
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return;
    // TESObjectREFR::Disable (19801, vfunc DisableImpl), as the world state's enabled row applies it.
    if (!pActor->IsDisabled())
        pActor->DisableImpl();
    if (aDelete)
        pActor->SetDelete(true);
}

void SkyrimBridge::SetActorIgnoreFriendlyHit(uint32_t aFormId, bool aIgnore) noexcept
{
    Breadcrumbs::Record("SetActorIgnoreFriendlyHit", aFormId, static_cast<uint32_t>(aIgnore));
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetIgnoreFriendlyHit(aIgnore);
}

void SkyrimBridge::SetActorNotOnStealthMeter(uint32_t aFormId, bool aNotShown) noexcept
{
    Breadcrumbs::Record("SetActorNotOnStealthMeter", aFormId, static_cast<uint32_t>(aNotShown));
    if (Actor* pActor = GetActor(aFormId))
        pActor->SetNotShowOnStealthMeter(aNotShown);
}

void SkyrimBridge::SetActorPlayerFaction(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("SetActorPlayerFaction", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->JoinPlayerFaction();
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

void SkyrimBridge::ActorFixVampireLordModel(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("ActorFixVampireLordModel", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->FixVampireLordModel();
}

void SkyrimBridge::ActorPutOnMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept
{
    Breadcrumbs::Record("ActorPutOnMount", aRiderFormId, aMountFormId);
    Actor* pRider = GetActor(aRiderFormId);
    Actor* pMount = GetActor(aMountFormId);
    if (pRider && pMount)
        pRider->PutOnMount(pMount);
}

void SkyrimBridge::ActorEndRide(uint32_t aRiderFormId) noexcept
{
    Breadcrumbs::Record("ActorEndRide", aRiderFormId);
    Actor* pRider = GetActor(aRiderFormId);
    if (pRider && pRider->actorState.GetSitSleepState() != ReplayState::kSitWantsToStand)
        pRider->EndRideNow();
}

void SkyrimBridge::ActorSeatOnMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept
{
    Breadcrumbs::Record("ActorSeatOnMount", aRiderFormId, aMountFormId);
    Actor* pRider = GetActor(aRiderFormId);
    Actor* pMount = GetActor(aMountFormId);
    if (pRider && pMount)
        pRider->SeatOnMount(pMount);
}

uint32_t SkyrimBridge::GetActorMount(uint32_t aRiderFormId) const noexcept
{
    const Actor* pRider = GetActor(aRiderFormId);
    const TESObjectREFR* pMount = pRider ? pRider->GetRiddenMount() : nullptr;
    return pMount ? pMount->formID : 0;
}

void SkyrimBridge::ActorHoldMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept
{
    Actor* pRider = GetActor(aRiderFormId);
    Actor* pMount = GetActor(aMountFormId);
    if (pRider && pMount)
        pMount->HoldRider(pRider, false);
}

uint8_t SkyrimBridge::GetActorSitState(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->actorState.GetSitSleepState() : 0;
}

void SkyrimBridge::ActorFinishDismount(uint32_t aRiderFormId) noexcept
{
    Breadcrumbs::Record("ActorFinishDismount", aRiderFormId);
    if (Actor* pRider = GetActor(aRiderFormId))
        pRider->FinishDismount();
}

void SkyrimBridge::ActorEndMountPair(uint32_t aMountFormId) noexcept
{
    if (Actor* pMount = GetActor(aMountFormId))
        pMount->EndMountPair();
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

uint32_t SkyrimBridge::CreateCustomActor(const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept
{
    Breadcrumbs::Record("CreateCustomActor", aChangeFlags);
    TESNPC* pNpc = TESNPC::Create(acAppearance, aChangeFlags);
    if (!pNpc)
        return 0;
    pNpc->weight = aWeight; // before the actor's 3D, which takes its build from it
    Actor* pActor = Actor::Create(pNpc);
    return pActor ? pActor->formID : 0;
}

bool SkyrimBridge::ApplyActorLooks(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept
{
    Actor* pActor = GetActor(aFormId);
    TESNPC* pNpc = pActor ? Cast<TESNPC>(pActor->baseForm) : nullptr;
    if (!pNpc || !pNpc->IsTemporary() || !pActor->GetExtension()->IsCopy() || !FormState::Load(*pNpc, aChangeFlags, acAppearance))
        return false;
    pNpc->weight = aWeight;
    if (pActor->race != pNpc->raceForm.race)
        pActor->SwitchRace(pNpc->raceForm.race, false);
    else
        pActor->DoReset3D(true);
    return true;
}

uint32_t SkyrimBridge::SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept
{
    Breadcrumbs::Record("SerializeActorNpc", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return 0;
    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    if (!pNpc)
        return 0;
    const uint32_t cFlags = NpcSnapshot::SaveFlags(pNpc->GetChangeFlags(), pActor == PlayerCharacter::Get(),
                                                 pNpc->IsTemporary(), pNpc->GetTemplateBase() != nullptr);
    FormState::Save(*pNpc, cFlags, aBuffer);
    return cFlags;
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

bool SkyrimBridge::IsActorLeveled(uint32_t aFormId) const noexcept
{
    const auto* pExtra = GetLeveledCreature(GetActor(aFormId));
    return pExtra && pExtra->originalBase;
}

// As ExtraDataList::LoadGame's ExtraLeveledCreature case (0x140177F8F): 14375(original, pick), the pick recorded, the
// new base set. The old temporary is left: a queued 3D load can still read it (leveled-copy-base.md).
bool SkyrimBridge::RelevelActor(uint32_t aFormId, uint32_t aPickId) noexcept
{
    Breadcrumbs::Record("RelevelActor", aFormId, aPickId);
    Actor* pActor = GetActor(aFormId);
    auto* pExtra = GetLeveledCreature(pActor);
    auto* pOriginal = pExtra ? Cast<TESNPC>(pExtra->originalBase) : nullptr;
    auto* pPick = Cast<TESNPC>(TESForm::GetById(aPickId));
    if (!pOriginal || !pPick)
        return false;
    TESNPC* pBuilt = TESNPC::BuildLeveled(pOriginal, pPick);
    if (!pBuilt)
        return false;
    pExtra->templateBase = pPick;
    pActor->GetExtension()->GraphDescriptorHash = 0;
    pActor->SetBaseObject(pBuilt);
    return true;
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

bool SkyrimBridge::IsActorFacePainted(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor && FaceGenSystem::IsPainted(pActor);
}

bool SkyrimBridge::PaintActorFace(uint32_t aFormId, const Tints& acTints) noexcept
{
    Breadcrumbs::Record("PaintActorFace", aFormId);
    Actor* pActor = GetActor(aFormId);
    return pActor && FaceGenSystem::Paint(pActor, acTints);
}
