#include <Bridges/GuestEngine.h>

#include <Bridges/IBodyBridge.h>

#include <Bridges/ContainerItems.h>
#include <Bridges/LooseItems.h>
#include <Games/Skyrim/PlayerHands.h>
#include <AiStateEvents.h>
#include <EffectEvents.h>
#include <FactionEvents.h>
#include <PlayerEvents.h>
#include <ReferenceEvents.h>
#include <ActorValueEvents.h>
#include <ContainerEvents.h>

#include <Breadcrumbs.h>
#include <Games/Overrides.h>
#include <Games/ReferenceStates.h>
#include <FormIds.h>
#include <SessionIds.h>

#include <ExtraData/ExtraContainerChanges.h>
#include <Forms/BGSLocation.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESWeather.h>
#include <Misc/BGSStoryManager.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/WorldEvents.h>
#include <Utils.h>
#include <Games/TES.h>
#include <Sky/Sky.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>
#include <TimeManager.h>
#include <RTTI.h>

namespace
{
// Running or starting.
constexpr uint16_t kStartedQuestFlags = TESQuest::Enabled | TESQuest::StageWait;

// The quest's flags under aMask set to aFlags', the change recorded.
void SetQuestFlags(TESQuest& aQuest, uint16_t aMask, uint16_t aFlags) noexcept
{
    const uint16_t cFlags = (aQuest.flags & ~aMask) | (aFlags & aMask);
    if (cFlags == aQuest.flags)
        return;
    aQuest.flags = cFlags;
    aQuest.MarkChanged(TESQuest::kChangeQuestFlags);
}

// Every role emptied, as the engine's stop does (25049).
void ClearFills(TESQuest& aQuest) noexcept
{
    for (BGSBaseAlias* pAlias : aQuest.aliases)
    {
        if (!pAlias)
            continue;
        const uint32_t cId = pAlias->aliasId;
        if (aQuest.GetRefAlias(cId))
        {
            if (aQuest.GetAliasedRef(cId))
                aQuest.ClearRefAlias(pAlias);
        }
        else if (aQuest.GetAliasedLocation(cId))
            aQuest.ClearLocationAlias(pAlias);
    }
}

void ApplyStages(TESQuest& aQuest, const QuestState& acQuest, bool aAsLoad) noexcept
{
    bool changed = false;
    for (const uint16_t cIndex : acQuest.DoneStages)
    {
        TESQuest::Stage* pStage = aQuest.GetStage(cIndex);
        if (!pStage || pStage->IsDone())
            continue;
        // Live: the engine's SetStage (journal, completion, events), the fragment the host's game ran left out.
        if (!aAsLoad && aQuest.SetStageWithoutFragments(cIndex))
            continue;
        pStage->SetDone(true);
        changed = true;
    }
    for (TESQuest::Stage* pStage : aQuest.stages)
        if (pStage && pStage->IsDone() && !std::ranges::binary_search(acQuest.DoneStages, pStage->stageIndex))
        {
            pStage->SetDone(false);
            changed = true;
        }
    if (aQuest.currentStage != acQuest.CurrentStage)
    {
        aQuest.currentStage = acQuest.CurrentStage;
        changed = true;
    }
    if (changed)
        aQuest.MarkChanged(TESQuest::kChangeStages);
}

void ApplyObjectives(TESQuest& aQuest, const QuestState& acQuest, bool aAsLoad) noexcept
{
    bool changed = false;
    for (TESQuest::Objective* pObjective : aQuest.objectives)
    {
        if (!pObjective)
            continue;
        // Hidden ones are not sent.
        const auto cSent = std::ranges::find(acQuest.Objectives, pObjective->index, &QuestObjective::Index);
        const uint8_t cWanted = cSent != acQuest.Objectives.end() ? cSent->State : 0;
        if (pObjective->state == cWanted)
            continue;
        if (aAsLoad)
        {
            pObjective->state = cWanted;
            changed = true;
        }
        else
            pObjective->SetState(cWanted);
    }
    if (changed)
        aQuest.MarkChanged(TESQuest::kChangeObjectives);
}

TESObjectREFR* FindReference(const GameId& acId) noexcept
{
    return Cast<TESObjectREFR>(TESForm::GetById(ToGame(acId)));
}

// Toggled only from the state the host's door was in before: one already where it went is left.
bool ActivateDoor(TESObjectREFR& aDoor, const ObjectChange& acChange) noexcept
{
    auto* pActivator = FindReference(acChange.Activator);
    if (!pActivator)
        return false;
    if (aDoor.GetOpenState() == static_cast<TESObjectREFR::OpenState>(acChange.OpenState))
        aDoor.Activate(pActivator, 0, nullptr, 1, 0);
    return true;
}

// As the host's script played it: PlayAnimationAndWait (56206) when it named an animation, else PlayAnimation (56205).
void PlayAnimation(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    BSFixedString event(acChange.Event.c_str());
    if (acChange.Animation.empty())
        aObject.PlayAnimation(&event);
    else
    {
        BSFixedString animation(acChange.Animation.c_str());
        aObject.PlayAnimationAndWait(&animation, &event);
    }
}

} // namespace

GuestEngine::GuestEngine(IBodyBridge& aBodies) noexcept
    : m_bodyBridge(aBodies)
    , m_moving(false)
{
    m_bodyBridge.SetReferences([this](const GameId& acId) {
        const TESObjectREFR* pObject = FindReference(acId);
        return pObject ? pObject->formID : 0u;
    });
    PlayerCharacter::SetPickUpGate([this](TESObjectREFR& aObject) { return IsPartyItem(aObject); });
    ContainerEvents::Start();
    PlayerHands::Watch(true);
    AiStateEvents::Start();
    EffectEvents::Start();
    PlayerEvents::Start();
    FactionEvents::Start();
    ReferenceEvents::Start();
    ActorValueEvents::Start();
}

GuestEngine::~GuestEngine() noexcept
{
    m_bodyBridge.SetReferences({});
    PlayerCharacter::SetPickUpGate({});
    // The host's created objects go with the session: a next one makes them again.
    for (const auto& [cHostId, cHandle] : m_created)
        if (TESObjectREFR* pMade = TESObjectREFR::GetByHandle(cHandle); pMade && !pMade->IsDeleted())
        {
            pMade->DisableImpl();
            pMade->SetDelete(true);
        }
    ContainerEvents::Stop();
    PlayerHands::Watch(false);
    AiStateEvents::Stop();
    EffectEvents::Stop();
    PlayerEvents::Stop();
    FactionEvents::Stop();
    ReferenceEvents::Stop();
    ActorValueEvents::Stop();
}

void GuestEngine::SetCalendar(const TimeModel& acTime) noexcept
{
    TimeData* pTime = TimeData::Get();
    if (!pTime || !pTime->GameYear || !pTime->GameMonth || !pTime->GameDay || !pTime->GameHour || !pTime->GameDaysPassed || !pTime->TimeScale)
        return;
    pTime->TimeScale->f = acTime.TimeScale;
    pTime->GameYear->f = static_cast<float>(acTime.Year);
    pTime->GameMonth->f = static_cast<float>(acTime.Month);
    pTime->GameDay->f = static_cast<float>(acTime.Day);
    pTime->GameHour->f = acTime.Time;
    // Whole days in rawDaysPassed, the day's part from the hour: as Calendar::Update writes them.
    pTime->rawDaysPassed = static_cast<float>(acTime.DaysPassed);
    pTime->GameDaysPassed->f = pTime->rawDaysPassed + acTime.Time / 24.f;
}

bool GuestEngine::HoldWeather(const GameId& acWeather) noexcept
{
    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(ToGame(acWeather)));
    Sky* pSky = Sky::Get();
    if (!pWeather || !pSky)
        return pWeather != nullptr;
    // The game changes its weather in many places (no single SetWeather): put the host's back whenever it did.
    const TESWeather* pShown = pSky->GetWeather();
    if (pShown != pWeather && !(pShown && pShown->formID == Sky::kWorldMapWeather))
    {
        Breadcrumbs::Record("HoldWeather", pWeather->formID);
        pSky->ForceWeather(pWeather);
    }
    return true;
}

void GuestEngine::ReleaseWeather() noexcept
{
    Breadcrumbs::Record("ReleaseWeather", 0);
    if (Sky* pSky = Sky::Get())
        pSky->ReleaseWeatherOverride();
}

bool GuestEngine::RunPlayerNative(const Payloads::RoutedCall& acCall, std::function<void(LocalValue)> aOnReturn) noexcept
{
    const auto cCall = Utils::ToLocalCall(acCall.Call);
    return cCall && GlobalCall::QueueCall(*cCall, std::move(aOnReturn));
}

bool GuestEngine::SetGlobal(const GlobalValue& acGlobal) noexcept
{
    auto* pGlobal = Cast<TESGlobal>(TESForm::GetById(ToGame(acGlobal.Id)));
    if (!pGlobal || pGlobal->IsConstant())
        return false;
    pGlobal->f = acGlobal.Value;
    return true;
}

void GuestEngine::ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept
{
    auto* pQuest = Cast<TESQuest>(TESForm::GetById(ToGame(acQuest.Id)));
    if (!pQuest)
        return;
    TESQuest& quest = *pQuest;

    // Running or starting first, without this game's own start: no alias rolls, no start-up stage of its own.
    if (acQuest.Flags & kStartedQuestFlags)
        SetQuestFlags(quest, kStartedQuestFlags, acQuest.Flags);
    if (ApplyFills(quest, acQuest))
        m_pendingAliases.erase(quest.formID);
    else
        m_pendingAliases[quest.formID] = acQuest;
    ApplyStages(quest, acQuest, aAsLoad);
    ApplyObjectives(quest, acQuest, aAsLoad);

    if (aAsLoad)
    {
        SetQuestFlags(quest, QuestState::kFlags, acQuest.Flags);
        return;
    }
    // Live: completed and failed through the engine's calls (journal, HUD), then stopped if the host's is.
    const auto cDiffers = [&](uint16_t aFlag) { return (acQuest.Flags & aFlag) != (quest.flags & aFlag); };
    if (cDiffers(TESQuest::Completed))
        quest.SetCompleted(acQuest.Flags & TESQuest::Completed);
    if (cDiffers(TESQuest::Failed))
        quest.SetFailed(acQuest.Flags & TESQuest::Failed);
    SetQuestFlags(quest, kStartedQuestFlags, acQuest.Flags);
}

void GuestEngine::SetDecidesWorld(bool aOn) noexcept
{
    BGSStoryManager::SetPicksQuests(aOn);
    WorldEvents::SetDelivered(aOn);
    ExtraContainerChanges::Data::SetRollsWorldContainers(aOn);
    TESObjectREFR::SetPicksLeveledBases(aOn);
}

std::optional<ObjectChange> GuestEngine::ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept
{
    return ::ReadContainerItems(aObjectFormId, aItemFormId);
}

TESObjectREFR* GuestEngine::FindFillReference(TESQuest& aQuest, const QuestAliasFill& acFill) const noexcept
{
    if (acFill.ActorId)
        return Cast<Actor>(TESForm::GetById(Utils::GetFormIdBySessionId(acFill.ActorId)));
    if (TESObjectREFR* pRef = FindReference(acFill.Ref))
        return pRef;
    const uint32_t cCellId = acFill.Cell.BaseId ? ToGame(acFill.Cell) : 0;
    return cCellId ? TESObjectREFR::LoadFromPlugin(ToGame(acFill.Ref), cCellId, acFill.GridX, acFill.GridY, &aQuest) : nullptr;
}

bool GuestEngine::ApplyFills(TESQuest& aQuest, const QuestState& acQuest) const noexcept
{
    bool ready = true;
    // A stopped quest's roles are empty on the host, so none come.
    if (!(acQuest.Flags & kStartedQuestFlags))
        ClearFills(aQuest);
    for (const auto& cFill : acQuest.Aliases)
    {
        BGSBaseAlias* pAlias = aQuest.GetRefAlias(cFill.AliasId);
        if (!pAlias || cFill.HostOnly)
            continue;
        TESObjectREFR* pRef = cFill.Ref.BaseId || cFill.ActorId ? FindFillReference(aQuest, cFill) : nullptr;
        if ((cFill.Ref.BaseId || cFill.ActorId) && !pRef)
        {
            ready &= !cFill.ActorId;
            continue;
        }
        if (aQuest.GetAliasedRef(cFill.AliasId) == pRef)
            continue;
        if (pRef)
            aQuest.ForceRefIntoAlias(cFill.AliasId, pRef);
        else
            aQuest.ClearRefAlias(pAlias);
    }
    for (const auto& cFill : acQuest.Locations)
    {
        auto* pLocation = cFill.Location.BaseId ? Cast<BGSLocation>(TESForm::GetById(ToGame(cFill.Location))) : nullptr;
        BGSBaseAlias* pAlias = aQuest.GetAlias(cFill.AliasId);
        if (!pAlias || aQuest.GetAliasedLocation(cFill.AliasId) == pLocation)
            continue;
        if (pLocation)
            aQuest.ForceLocationIntoAlias(cFill.AliasId, pLocation);
        else
            aQuest.ClearLocationAlias(pAlias);
    }
    return ready;
}

void GuestEngine::RetryQuestAliases() noexcept
{
    for (auto it = m_pendingAliases.begin(); it != m_pendingAliases.end();)
    {
        auto* pQuest = Cast<TESQuest>(TESForm::GetById(it->first));
        if (!pQuest || ApplyFills(*pQuest, it->second))
            it = m_pendingAliases.erase(it);
        else
            ++it;
    }
}



TESObjectREFR* GuestEngine::FindReference(const GameId& acId) const noexcept
{
    if (acId.ModId != ObjectChange::kCreatedModId)
        return ::FindReference(acId);
    const auto it = m_created.find(acId.BaseId);
    return it != m_created.end() ? TESObjectREFR::GetByHandle(it->second) : nullptr;
}

bool GuestEngine::Create(const ObjectChange& acChange) noexcept
{
    if (const TESObjectREFR* pMade = FindReference(acChange.Object); pMade && !pMade->IsDeleted())
        return true;
    ObjectChange::Placement placement;
    if (!ObjectChange::UnpackPlacement(acChange.Data, placement))
        return false;
    // An item with its count and extra data (a drop), else its base alone.
    TESObjectREFR* pMade = MakeLooseItem(acChange.Item, acChange.Stacks.empty() ? nullptr : &acChange.Stacks[0], placement);
    if (!pMade)
        return false;
    m_created[acChange.Object.BaseId] = pMade->GetHandle().handle.iBits;
    return true;
}

std::optional<DropItemRequest> GuestEngine::ReadDrop(uint32_t aFormId) noexcept
{
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    const auto cItem = pObject ? ReadLooseItem(*pObject) : std::nullopt;
    const auto cPlacement = cItem ? ReadPlacement(*pObject) : std::nullopt;
    if (!cPlacement)
        return std::nullopt;
    m_ownDrops[aFormId] = pObject->GetHandle().handle.iBits;
    DropItemRequest drop;
    drop.Tag = aFormId;
    drop.Item = *cItem;
    drop.Placement = *cPlacement;
    return drop;
}

bool GuestEngine::AdoptDrop(uint32_t aTag, const GameId& acObject) noexcept
{
    const auto it = m_ownDrops.find(aTag);
    if (it == m_ownDrops.end())
        return false;
    const uint32_t cHandle = it->second;
    m_ownDrops.erase(it);
    const TESObjectREFR* pOwn = TESObjectREFR::GetByHandle(cHandle);
    if (!pOwn || pOwn->IsDeleted())
        return false;
    if (acObject == GameId{})
        return true;
    m_created[acObject.BaseId] = cHandle;
    m_adopted.push_back({.Object = acObject});
    return true;
}

std::vector<BodyKey> GuestEngine::TakeAdoptedDrops() noexcept
{
    return std::exchange(m_adopted, {});
}

bool GuestEngine::IsPartyItem(const TESObjectREFR& acObject) const noexcept
{
    if (!IsCreatedForm(acObject.formID))
        return false;
    if (m_ownDrops.contains(acObject.formID))
        return true;
    return std::ranges::any_of(m_created, [&](const auto& acCreated) { return TESObjectREFR::GetByHandle(acCreated.second) == &acObject; });
}

std::optional<PickUpRequest> GuestEngine::ReadPickUp(uint32_t aFormId) const noexcept
{
    const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    if (!pObject || !IsPartyItem(*pObject))
        return std::nullopt;
    PickUpRequest request;
    if (m_ownDrops.contains(aFormId))
        request.Tag = aFormId;
    else
        request.Object = HostIdOf(*pObject).value_or(GameId{});
    return request;
}

void GuestEngine::PickUp(const NotifyPickUp& acGrant) noexcept
{
    uint32_t handle = 0;
    if (const auto it = m_created.find(acGrant.Object.BaseId); acGrant.Object.ModId == ObjectChange::kCreatedModId && it != m_created.end())
    {
        handle = it->second;
        m_created.erase(it);
        m_taken.insert(acGrant.Object.BaseId);
    }
    if (const auto it = m_ownDrops.find(acGrant.Tag); acGrant.Tag && it != m_ownDrops.end())
    {
        handle = it->second;
        m_ownDrops.erase(it);
    }
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;
    TESObjectREFR* pTwin = handle ? TESObjectREFR::GetByHandle(handle) : nullptr;
    if (pTwin && !pTwin->IsDeleted() && !pTwin->IsDisabled())
    {
        pPlayer->PickUpGranted(*pTwin, ReadLooseItem(*pTwin).value_or(acGrant.Item).Count);
        return;
    }
    // Its twin is gone here (the host's deletion came first): the item itself, as a pickup raises it.
    pPlayer->AddOrRemoveItem(acGrant.Item);
}

bool GuestEngine::ApplyObjectChange(const ObjectChange& acChange, bool aLoad) noexcept
{
    // The host's change, already sent: our hooks raise no events for it, so nothing goes back.
    ScopedHostChangeOverride _;
    if (acChange.Type == ObjectChange::Kind::kCreate)
        return Create(acChange);
    auto* pObject = FindReference(acChange.Object);
    // The host's deletion of one this game's player picked up: done already.
    if (!pObject && acChange.Object.ModId == ObjectChange::kCreatedModId && m_taken.erase(acChange.Object.BaseId))
        return true;
    if (!pObject)
        return false;
    if (aLoad && ReferenceStates::Matches(*pObject, acChange))
        return true;
    switch (acChange.Type)
    {
    case ObjectChange::Kind::kMove: return PlaceAtHostRest(*pObject, acChange);
    case ObjectChange::Kind::kActivate: return ActivateDoor(*pObject, acChange);
    case ObjectChange::Kind::kAnimation: PlayAnimation(*pObject, acChange); return true;
    case ObjectChange::Kind::kRecord:
        ReferenceStates::RecordRows(*pObject, acChange.Value);
        return true;
    case ObjectChange::Kind::kContents: return ApplyContainerContents(*pObject, acChange);
    case ObjectChange::Kind::kItems: return ApplyContainerItems(*pObject, acChange);
    default: return ReferenceStates::Apply(*pObject, acChange);
    }
}

std::vector<uint32_t> GuestEngine::AttachedCells() const noexcept
{
    std::vector<uint32_t> cells;
    if (const TES* pTes = TES::Get())
        for (const TESObjectCELL* pCell : pTes->AttachedCells())
            if (pCell->IsFullyLoaded())
                cells.push_back(pCell->formID);
    return cells;
}

std::optional<CellStateRequest> GuestEngine::ReadCellChanges(uint32_t aCellFormId) const noexcept
{
    auto* pCell = Cast<TESObjectCELL>(TESForm::GetById(aCellFormId));
    const auto cCell = pCell && pCell->IsValid() ? ToSession(aCellFormId) : std::nullopt;
    if (!cCell)
        return std::nullopt;
    CellStateRequest request;
    request.Cell = *cCell;
    for (const TESObjectREFR* pRef : pCell->GetReferences())
    {
        ChangedReference changed;
        changed.Flags = ReferenceStates::ChangedRows(*pRef);
        if (const auto cId = changed.Flags ? ToSession(pRef->formID) : std::nullopt)
        {
            changed.Id = *cId;
            request.Changed.push_back(changed);
        }
    }
    return request;
}

bool GuestEngine::IsCellAttached(const GameId& acCell) const noexcept
{
    const auto* pCell = Cast<TESObjectCELL>(TESForm::GetById(ToGame(acCell)));
    return pCell && pCell->IsValid();
}

bool GuestEngine::IsPlacementPending(const GameId& acObject) const noexcept
{
    return std::ranges::any_of(m_placed, [&](const auto& acPlaced) { return acPlaced.first.Object == acObject; });
}
