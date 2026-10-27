#include <Bridges/HostEngine.h>
#include <MainLoopTasks.h>
#include <AI/FollowerTargets.h>
#include <SessionIds.h>

#include <Bridges/ContainerItems.h>
#include <Bridges/LooseItems.h>
#include <Bridges/IBodyBridge.h>
#include <AiStateEvents.h>
#include <EffectEvents.h>
#include <FactionEvents.h>
#include <PlayerEvents.h>
#include <ReferenceEvents.h>
#include <ActorValueEvents.h>
#include <QuestEvents.h>
#include <GlobalEvents.h>
#include <ContainerEvents.h>

#include <Breadcrumbs.h>
#include <FormIds.h>

#include <Forms/FormChanges.h>
#include <Forms/BGSLocation.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWeather.h>
#include <Games/ReferenceStates.h>
#include <Games/TES.h>
#include <SaveLoad.h>
#include <Sky/Sky.h>
#include <Actor.h>
#include <TESObjectREFR.h>
#include <TimeManager.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <mutex>
#include <utility>
#include <RTTI.h>

static_assert(QuestState::kFlags == (TESQuest::Enabled | TESQuest::Completed | TESQuest::Failed | TESQuest::StageWait));

namespace
{
// A role's fill as a guest can make it (QuestAliasFill): acCreated, the created references that went out.
QuestAliasFill ReadAliasFill(uint32_t aAliasId, const TESObjectREFR* apRef,
                             const std::unordered_map<uint32_t, uint32_t>& acCreated) noexcept
{
    QuestAliasFill fill{aAliasId};
    if (!apRef)
        return fill;
    if (apRef->IsTemporary())
    {
        fill.HostOnly = !acCreated.count(apRef->formID);
        fill.Ref = CreatedId(apRef->formID);
        return fill;
    }
    const TESObjectCELL* pCell = apRef->GetParentCellEx();
    const auto cRef = ToSession(apRef->formID);
    if (!cRef)
        return fill;
    fill.Ref = *cRef;
    if (!pCell)
        return fill;
    int32_t x = 0;
    int32_t y = 0;
    const bool cOutdoors = pCell->worldspace && pCell->GetGrid(x, y);
    fill.Cell = ToSession(cOutdoors ? pCell->worldspace->formID : pCell->formID).value_or(GameId{});
    fill.GridX = static_cast<int16_t>(x);
    fill.GridY = static_cast<int16_t>(y);
    return fill;
}

QuestState ReadQuest(TESQuest& aQuest, const GameId& acId, const std::unordered_map<uint32_t, uint32_t>& acCreated) noexcept
{
    QuestState quest{acId, static_cast<uint16_t>(aQuest.flags & QuestState::kFlags), aQuest.currentStage};
    for (const TESQuest::Stage* pStage : aQuest.stages)
        if (pStage && pStage->IsDone())
            quest.DoneStages.push_back(pStage->stageIndex);
    std::ranges::sort(quest.DoneStages);
    for (const TESQuest::Objective* pObjective : aQuest.objectives)
        if (pObjective && pObjective->state)
            quest.Objectives.push_back({pObjective->index, pObjective->state});
    // A stopped quest has its aliases cleared (25049): only a running or starting one's are read.
    if (aQuest.IsStopped())
    {
        QuestEvents::Changes().Track(aQuest.formID, {});
        return quest;
    }
    std::set<uint32_t> references;
    for (const BGSBaseAlias* pAlias : aQuest.aliases)
    {
        if (!pAlias)
            continue;
        if (aQuest.GetRefAlias(pAlias->aliasId))
        {
            const auto* pRef = aQuest.GetAliasedRef(pAlias->aliasId);
            if (pRef)
                references.insert(pRef->formID);
            quest.Aliases.push_back(ReadAliasFill(pAlias->aliasId, pRef, acCreated));
            continue;
        }
        QuestLocationFill& fill = quest.Locations.emplace_back(QuestLocationFill{pAlias->aliasId});
        if (const BGSLocation* pLocation = aQuest.GetAliasedLocation(pAlias->aliasId))
            fill.Location = ToSession(pLocation->formID).value_or(GameId{});
    }
    QuestEvents::Changes().Track(aQuest.formID, references);
    return quest;
}

// What the engine did to references since the last update, filled on its threads (the change funnel and the
// created-reference hook); one HostEngine at a time. The lock is a leaf.
struct EngineLog
{
    struct Taken
    {
        std::vector<uint32_t> Created;          // in order
        std::unordered_map<uint32_t, uint32_t> Changed;   // form id: change flags, merged
        std::unordered_set<uint32_t> Removed;             // records that lost flags
    };

    void Created(uint32_t aFormId) noexcept
    {
        std::scoped_lock _(m_lock);
        m_taken.Created.push_back(aFormId);
    }

    void Changed(uint32_t aFormId, uint32_t aFlags, bool aRemoved) noexcept
    {
        std::scoped_lock _(m_lock);
        m_taken.Changed[aFormId] |= aFlags;
        if (aRemoved)
            m_taken.Removed.insert(aFormId);
    }

    Taken Take() noexcept
    {
        std::scoped_lock _(m_lock);
        return std::exchange(m_taken, {});
    }

private:
    std::mutex m_lock;
    Taken m_taken;
};

EngineLog s_log;

void OnCreated(uint32_t aFormId) noexcept
{
    s_log.Created(aFormId);
}

// A world reference's recorded change (not an actor's, not a load's forced one). Removed: the record lost the flags
// (a state back to its default, or the engine dropping changes on a long wait).
void OnFormChange(const TESForm& acForm, uint32_t aFlags, bool aForced, bool aRecorded) noexcept
{
    const uint32_t cFlags = aFlags & ReferenceStates::SentFlags();
    if (!cFlags || !aRecorded || aForced || acForm.formType != FormType::Reference)
        return;
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    s_log.Changed(acForm.formID, cFlags, pSaveLoad && !pSaveLoad->HasChange(acForm, cFlags));
}

// A world object the host's game made (not an actor, a projectile or an explosion), as the guests make theirs: an
// item with its count and extra data (a drop).
std::optional<ObjectChange> ReadCreate(TESObjectREFR& aRef) noexcept
{
    if (aRef.formType != FormType::Reference || aRef.IsDeleted() || !aRef.baseForm || !Cast<TESBoundObject>(aRef.baseForm))
        return std::nullopt;
    const auto cObject = ToSessionOrCreated(aRef.formID);
    const auto cItem = ToSession(aRef.baseForm->formID);
    const auto cPlacement = ReadPlacement(aRef);
    if (!cObject || !cItem || !cPlacement)
        return std::nullopt;
    ObjectChange change{ObjectChange::Kind::kCreate, *cObject};
    change.Item = *cItem;
    change.Data = ObjectChange::PackPlacement(*cPlacement);
    if (const auto cLoose = ReadLooseItem(aRef); cLoose && (cLoose->Count != 1 || cLoose->ContainsExtraData()))
        change.Stacks.push_back(*cLoose);
    return change;
}

// The rows the host's record holds: a guest's record is made the same.
ObjectChange ReadRecord(const TESObjectREFR& acRef, const GameId& acId) noexcept
{
    ObjectChange record{ObjectChange::Kind::kRecord, acId};
    record.Value = ReferenceStates::RecordedRows(acRef);
    return record;
}
} // namespace

HostEngine::HostEngine(IBodyBridge& aBodies) noexcept
    : m_bodyBridge(aBodies)
{
    // The bodies of the references the guests have (IBodyBridge: a guest's player's hand on them).
    m_bodyBridge.SetReferences([this](const GameId& acId) {
        const uint32_t cFormId = acId.ModId == ObjectChange::kCreatedModId ? 0xFF000000 | acId.BaseId : ToGame(acId);
        return cFormId && IsShared(cFormId) ? cFormId : 0u;
    });
    if (!FormChanges::Watch(&OnFormChange))
        spdlog::error("{}: no slot left to watch the change funnel, the references' state stays here", __FUNCTION__);
    FollowerTargets::Start();
    ContainerEvents::Start();
    AiStateEvents::Start();
    EffectEvents::Start();
    PlayerEvents::Start();
    FactionEvents::Start();
    ReferenceEvents::Start();
    ActorValueEvents::Start();
    QuestEvents::Start();
    GlobalEvents::Start();
    ModManager::WatchCreated(&OnCreated);
}

HostEngine::~HostEngine() noexcept
{
    m_bodyBridge.SetReferences({});
    ModManager::WatchCreated(nullptr);
    FormChanges::Unwatch(&OnFormChange);
    s_log.Take();
    FollowerTargets::Stop();
    ContainerEvents::Stop();
    AiStateEvents::Stop();
    EffectEvents::Stop();
    PlayerEvents::Stop();
    FactionEvents::Stop();
    ReferenceEvents::Stop();
    ActorValueEvents::Stop();
    QuestEvents::Stop();
    GlobalEvents::Stop();
}

std::optional<GameId> HostEngine::ReadWeather() const noexcept
{
    const Sky* pSky = Sky::Get();
    const TESWeather* pWeather = pSky ? pSky->GetWeather() : nullptr;
    if (!pWeather || pWeather->formID == Sky::kWorldMapWeather)
        return std::nullopt;
    return ToSession(pWeather->formID);
}

std::vector<HostGlobal> HostEngine::ReadGlobals() const noexcept
{
    std::vector<HostGlobal> globals;
    auto* pData = ModManager::Get();
    if (!pData)
        return globals;
    globals.reserve(pData->globals.length);
    for (TESGlobal* pGlobal : pData->globals)
    {
        if (!pGlobal || pGlobal->IsConstant())
            continue;
        if (const auto cId = ToSession(pGlobal->formID))
            globals.push_back({*cId, pGlobal->f, TimeData::IsCalendarGlobal(pGlobal->formID)});
    }
    return globals;
}

std::vector<HostGlobal> HostEngine::TakeGlobalChanges() const noexcept
{
    std::vector<HostGlobal> globals;
    for (const uint32_t cId : GlobalEvents::Take())
        if (const auto* pGlobal = Cast<TESGlobal>(TESForm::GetById(cId)); pGlobal && !pGlobal->IsConstant())
            if (const auto cSessionId = ToSession(cId))
                globals.push_back({*cSessionId, pGlobal->f, TimeData::IsCalendarGlobal(cId)});
    return globals;
}

std::vector<QuestState> HostEngine::ReadQuests() const noexcept
{
    std::vector<QuestState> quests;
    auto* pData = ModManager::Get();
    if (!pData)
        return quests;
    quests.reserve(pData->quests.length);
    for (TESQuest* pQuest : pData->quests)
    {
        if (const auto cId = pQuest ? ToSession(pQuest->formID) : std::nullopt)
            quests.push_back(ReadQuest(*pQuest, *cId, m_created));
    }
    return quests;
}

std::vector<QuestState> HostEngine::TakeQuestChanges() const noexcept
{
    std::vector<QuestState> quests;
    for (const uint32_t cId : QuestEvents::Changes().Take())
        if (auto* pQuest = Cast<TESQuest>(TESForm::GetById(cId)))
            if (const auto cSessionId = ToSession(cId))
                quests.push_back(ReadQuest(*pQuest, *cSessionId, m_created));
    return quests;
}

void HostEngine::SetHour(float aHours) noexcept
{
    Breadcrumbs::Record("SetHour", 0);
    TimeData* pTime = TimeData::Get();
    if (!pTime || !pTime->GameHour)
        return;
    pTime->Advance(std::fmod(aHours - pTime->GameHour->f + 24.f, 24.f));
}

void HostEngine::RecordActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aOpenState) noexcept
{
    const auto cObject = ToSessionOrCreated(aObjectFormId);
    const auto cActivator = ToSessionOrCreated(aActivatorFormId);
    if (!cObject || !cActivator)
        return;
    ObjectChange event{ObjectChange::Kind::kActivate, *cObject};
    event.OpenState = aOpenState;
    event.Activator = *cActivator;
    m_events.push_back(std::move(event));
}

void HostEngine::RecordAnimation(uint32_t aObjectFormId, const std::string& acAnimation, const std::string& acEvent) noexcept
{
    const auto cObject = ToSessionOrCreated(aObjectFormId);
    if (!cObject)
        return;
    ObjectChange event{ObjectChange::Kind::kAnimation, *cObject};
    event.Animation = acAnimation;
    event.Event = acEvent;
    m_events.push_back(std::move(event));
}

void HostEngine::RecordItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept
{
    if (auto change = ReadContainerItems(aObjectFormId, aItemFormId))
        m_events.push_back(std::move(*change));
}

void HostEngine::SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept
{
    // Goes out from the funnel, as any of the host's lock changes.
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(ToGame(acObject)));
    if (pObject && pObject->SetLockState(aLocked, aLockLevel))
        spdlog::info("HostEngine: took a guest's lock {:X} {} at {}", acObject.LogFormat(), aLocked ? "locked" : "unlocked", aLockLevel);
    else
        spdlog::warn("HostEngine: a guest's lock {:X}: no such object here", acObject.LogFormat());
}

void HostEngine::SetItems(const ObjectChange& acItems) noexcept
{
    const uint32_t cObjectId = ToGame(acItems.Object);
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (pObject && ApplyContainerItems(*pObject, acItems))
    {
        spdlog::info("HostEngine: took a guest's items {:X} holds {} of {:X} ({} with extra data)", acItems.Object.LogFormat(), acItems.Value,
                     acItems.Item.LogFormat(), acItems.Stacks.size());
        RecordItems(cObjectId, ToGame(acItems.Item));
    }
    else
        spdlog::warn("HostEngine: a guest's items {:X} holds {} of {:X}: no such container or item here", acItems.Object.LogFormat(),
                     acItems.Value, acItems.Item.LogFormat());
}

std::optional<GameId> HostEngine::CreateItem(const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) noexcept
{
    if (!IsPlacementLoaded(acPlacement))
        return std::nullopt;
    TESObjectREFR* pMade = MakeLooseItem(acItem.BaseId, &acItem, acPlacement);
    if (!pMade)
        return GameId{};
    // Made past our watch of the engine's creations: it goes out with them.
    OnCreated(pMade->formID);
    return CreatedId(pMade->formID);
}

std::optional<Inventory::Entry> HostEngine::TakeItem(const GameId& acObject) noexcept
{
    // Not IsShared: a drop made for a guest this frame goes out with the next update, and may be asked for first.
    const uint32_t cFormId = acObject.ModId == ObjectChange::kCreatedModId ? 0xFF000000 | acObject.BaseId : 0;
    auto* pObject = cFormId ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr;
    if (!pObject || pObject->IsDeleted() || pObject->IsDisabled())
        return std::nullopt;
    auto item = ReadLooseItem(*pObject);
    if (!item)
        return std::nullopt;
    // Gone as ReferenceStates' deletion does it; TakeDeletedCreated sends the deletion.
    pObject->DisableImpl();
    pObject->SetDelete(true);
    return item;
}

std::vector<ObjectChange> HostEngine::TakeObjectChanges() noexcept
{
    auto [created, changed, removed] = s_log.Take();
    std::vector<ObjectChange> changes;
    TakeDeletedCreated(changes);
    // Created next: the changes after refer to them.
    for (const uint32_t cFormId : created)
    {
        auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        if (auto create = pObject ? ReadCreate(*pObject) : std::nullopt)
        {
            // An id still out: its old reference was deleted and the id used again since the last update.
            if (m_created.count(cFormId))
                changes.push_back(ReferenceStates::Deleted(create->Object));
            m_created[cFormId] = pObject->baseForm->formID;
            QuestEvents::Changes().Reference(cFormId);
            m_bodies.Forget(cFormId); // an id used before: the old one's rest is not this one's
            changes.push_back(std::move(*create));
        }
    }
    // Then the states, each as it is now.
    for (const auto& [cFormId, cFlags] : changed)
    {
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        const auto cId = pObject && IsShared(cFormId) ? ToSessionOrCreated(cFormId) : std::nullopt;
        if (!cId)
            continue;
        const GameId& id = *cId;
        // A physics move goes out once it rests (m_bodies), its flight as body frames (HostPhysics). A graph's live changes
        // are the animations scripts play (relayed as events); its state goes with a cell's.
        constexpr uint32_t cNotLive = TESObjectREFR::CHANGE_REFR_HAVOK_MOVE | TESObjectREFR::CHANGE_REFR_ANIMATION;
        ReferenceStates::Read(*pObject, id, cFlags & ~cNotLive, changes);
    }
    for (const uint32_t cFormId : m_bodies.TakeRested())
        if (const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId)))
            if (const auto cId = IsShared(cFormId) ? ToSessionOrCreated(cFormId) : std::nullopt)
                ReferenceStates::Read(*pObject, *cId, TESObjectREFR::CHANGE_REFR_MOVE, changes);
    // Records that lost rows: the guests drop theirs too.
    for (const uint32_t cFormId : removed)
        if (const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId)))
            if (const auto cId = IsShared(cFormId) ? ToSessionOrCreated(cFormId) : std::nullopt)
                changes.push_back(ReadRecord(*pObject, *cId));
    // Events last: an object a script enabled and then animated is enabled for the animation.
    changes.insert(changes.end(), std::make_move_iterator(m_events.begin()), std::make_move_iterator(m_events.end()));
    m_events.clear();
    return changes;
}

bool HostEngine::IsShared(uint32_t aFormId) const noexcept
{
    return aFormId < 0xFF000000 || m_created.count(aFormId);
}

void HostEngine::TakeDeletedCreated(std::vector<ObjectChange>& aChanges) noexcept
{
    for (auto it = m_created.begin(); it != m_created.end();)
    {
        const auto [cFormId, cBaseId] = *it;
        const auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        if (pRef && !pRef->IsDeleted() && pRef->baseForm && pRef->baseForm->formID == cBaseId)
        {
            ++it;
            continue;
        }
        aChanges.push_back(ReferenceStates::Deleted(CreatedId(cFormId)));
        m_bodies.Forget(cFormId);
        QuestEvents::Changes().Reference(it->first);
        it = m_created.erase(it);
    }
}

std::optional<std::vector<ObjectChange>> HostEngine::ReadCellState(const GameId& acCell,
                                                                             const std::vector<ChangedReference>& acChanged) const noexcept
{
    auto* pCell = Cast<TESObjectCELL>(TESForm::GetById(ToGame(acCell)));
    if (!pCell || !pCell->IsFullyLoaded())
        return std::nullopt;
    std::unordered_map<uint32_t, uint32_t> guestRows;
    for (const auto& cRef : acChanged)
        if (const uint32_t cFormId = ToGame(cRef.Id))
            guestRows[cFormId] |= cRef.Flags;
    std::vector<ObjectChange> changes;
    const auto cReferences = pCell->GetReferences();
    // The host's created ones there first, so the guest makes its own, with their rows.
    for (TESObjectREFR* pRef : cReferences)
    {
        if (!pRef->IsTemporary() || !IsShared(pRef->formID))
            continue;
        if (auto create = ReadCreate(*pRef))
        {
            const GameId cId = create->Object;
            changes.push_back(std::move(*create));
            ReferenceStates::Read(*pRef, cId, ReferenceStates::RecordedRows(*pRef), changes);
        }
    }
    // Then the rows this game or the guest changed, and the host's record of them.
    for (const TESObjectREFR* pRef : cReferences)
    {
        const auto cGuest = guestRows.find(pRef->formID);
        const bool cGuestChanged = cGuest != guestRows.end() && pRef->formType == FormType::Reference;
        const uint32_t cRows = ReferenceStates::ChangedRows(*pRef) | (cGuestChanged ? cGuest->second & ReferenceStates::RowFlags() : 0);
        const auto cId = cRows ? ToSessionOrCreated(pRef->formID) : std::nullopt;
        if (!cId)
            continue;
        ReferenceStates::Read(*pRef, *cId, cRows, changes);
        changes.push_back(ReadRecord(*pRef, *cId));
    }
    // Containers last: the host's roll of their leveled entries, then all they hold.
    for (TESObjectREFR* pRef : cReferences)
    {
        if (pRef->formType != FormType::Reference || pRef->IsTemporary() || !pRef->GetContainer())
            continue;
        pRef->InitInventoryIfRequired();
        if (auto contents = ReadContainerContents(*pRef))
            changes.push_back(std::move(*contents));
    }
    return changes;
}

void HostEngine::AdvanceTime(float aHours) noexcept
{
    // The host may already be loading after following the guest. Run calendar work outside its VM update job.
    MainLoopTasks::Queue([aHours] {
        if (auto* pTime = TimeData::Get())
            pTime->Advance(aHours);
    });
}

std::optional<std::vector<Followers::State>> HostEngine::ReadFollowers() const noexcept
{
    auto* pQuest = Cast<TESQuest>(TESForm::GetById(0x750BA));
    if (!pQuest)
        return std::nullopt;
    std::vector<Followers::State> followers;
    for (uint32_t alias : {0u, 1u})
        if (auto* pActor = Cast<Actor>(pQuest->GetAliasedRef(alias)))
        {
            Followers::State state;
            state.Reference = pActor->formID;
            state.Session = Utils::GetSessionIdByFormId(pActor->formID).value_or(0);
            state.At.Position = {pActor->position.x, pActor->position.y, pActor->position.z};
            const auto* pCell = pActor->GetParentCellEx();
            state.At.Cell = pCell ? pCell->formID : 0;
            state.At.World = pCell && pCell->worldspace ? pCell->worldspace->formID : 0;
            state.Waiting = pActor->GetActorValue(95) != 0.f;
            state.Dead = pActor->IsDead();
            followers.push_back(state);
        }
    return followers;
}

std::vector<Followers::Order> HostEngine::TakeFollowerOrders() const noexcept { return FollowerTargets::TakeOrders(); }
void HostEngine::SetFollowerTargets(const std::vector<Followers::Target>& acTargets) const noexcept { FollowerTargets::Set(acTargets); }
