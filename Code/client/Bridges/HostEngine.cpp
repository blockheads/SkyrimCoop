#include <Bridges/HostEngine.h>

#include <Bridges/ContainerItems.h>
#include <ContainerEvents.h>

#include <Breadcrumbs.h>
#include <Systems/ModSystem.h>

#include <Forms/FormChanges.h>
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

static_assert(QuestState::kFlags == (TESQuest::Enabled | TESQuest::Completed | TESQuest::Failed | TESQuest::StageWait));

namespace
{
QuestState ReadQuest(TESQuest& aQuest, const GameId& acId, const ModSystem& acMods) noexcept
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
        return quest;
    for (const BGSBaseAlias* pAlias : aQuest.aliases)
    {
        if (!pAlias || !aQuest.GetRefAlias(pAlias->aliasId))
            continue;
        QuestAliasFill& fill = quest.Aliases.emplace_back(QuestAliasFill{pAlias->aliasId});
        const TESObjectREFR* pRef = aQuest.GetAliasedRef(pAlias->aliasId);
        // A reference the host's game created has an id of its own (docs/progress.md, loose ends).
        fill.Created = pRef && pRef->formID >= 0xFF000000;
        if (pRef && !fill.Created)
            acMods.GetServerModId(pRef->formID, fill.Ref);
    }
    return quest;
}

// What the engine did to references since the last update, filled on its threads (the change funnel and the
// created-reference hook); one HostEngine at a time. The lock is a leaf.
struct EngineLog
{
    struct Taken
    {
        TiltedPhoques::Vector<uint32_t> Created;          // in order
        TiltedPhoques::Map<uint32_t, uint32_t> Changed;   // form id: change flags, merged
        TiltedPhoques::Set<uint32_t> Removed;             // records that lost flags
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

// A world object the host's game made (not an actor, a projectile or an explosion), as the guests make theirs.
std::optional<ObjectChange> ReadCreate(const TESObjectREFR& acRef, const ModSystem& acMods) noexcept
{
    ObjectChange change{ObjectChange::Kind::kCreate};
    const TESObjectCELL* pCell = acRef.GetParentCellEx();
    if (acRef.formType != FormType::Reference || acRef.IsDeleted() || !acRef.baseForm || !Cast<TESBoundObject>(acRef.baseForm) || !pCell ||
        !acMods.GetServerModId(acRef.formID, change.Object) || !acMods.GetServerModId(acRef.baseForm->formID, change.Item))
        return std::nullopt;
    ObjectChange::Placement placement;
    if (pCell->worldspace)
        acMods.GetServerModId(pCell->worldspace->formID, placement.WorldSpace);
    else
        acMods.GetServerModId(pCell->formID, placement.Cell);
    for (int axis = 0; axis < 3; ++axis)
    {
        placement.Position[axis] = acRef.position[axis];
        placement.Rotation[axis] = acRef.rotation[axis];
    }
    change.Data = ObjectChange::PackPlacement(placement);
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

HostEngine::HostEngine(const ModSystem& acMods) noexcept
    : m_mods(acMods)
{
    if (!FormChanges::Watch(&OnFormChange))
        spdlog::error("{}: no slot left to watch the change funnel, the references' state stays here", __FUNCTION__);
    ContainerEvents::Start();
    ModManager::WatchCreated(&OnCreated);
}

HostEngine::~HostEngine() noexcept
{
    ModManager::WatchCreated(nullptr);
    FormChanges::Unwatch(&OnFormChange);
    s_log.Take();
    ContainerEvents::Stop();
}

std::optional<GameId> HostEngine::ReadWeather() const noexcept
{
    const Sky* pSky = Sky::Get();
    const TESWeather* pWeather = pSky ? pSky->GetWeather() : nullptr;
    GameId id;
    if (!pWeather || pWeather->formID == Sky::kWorldMapWeather || !m_mods.GetServerModId(pWeather->formID, id))
        return std::nullopt;
    return id;
}

TiltedPhoques::Vector<HostGlobal> HostEngine::ReadGlobals() const noexcept
{
    TiltedPhoques::Vector<HostGlobal> globals;
    auto* pData = ModManager::Get();
    if (!pData)
        return globals;
    globals.reserve(pData->globals.length);
    for (TESGlobal* pGlobal : pData->globals)
    {
        GameId id;
        if (pGlobal && !pGlobal->IsConstant() && m_mods.GetServerModId(pGlobal->formID, id))
            globals.push_back({id, pGlobal->f, TimeData::IsCalendarGlobal(pGlobal->formID)});
    }
    return globals;
}

TiltedPhoques::Vector<QuestState> HostEngine::ReadQuests() const noexcept
{
    TiltedPhoques::Vector<QuestState> quests;
    auto* pData = ModManager::Get();
    if (!pData)
        return quests;
    quests.reserve(pData->quests.length);
    for (TESQuest* pQuest : pData->quests)
    {
        GameId id;
        if (pQuest && m_mods.GetServerModId(pQuest->formID, id))
            quests.push_back(ReadQuest(*pQuest, id, m_mods));
    }
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
    ObjectChange event{ObjectChange::Kind::kActivate};
    event.OpenState = aOpenState;
    if (m_mods.GetServerModId(aObjectFormId, event.Object) && m_mods.GetServerModId(aActivatorFormId, event.Activator))
        m_events.push_back(std::move(event));
}

void HostEngine::RecordAnimation(uint32_t aObjectFormId, const TiltedPhoques::String& acAnimation, const TiltedPhoques::String& acEvent) noexcept
{
    ObjectChange event{ObjectChange::Kind::kAnimation};
    event.Animation = acAnimation;
    event.Event = acEvent;
    if (m_mods.GetServerModId(aObjectFormId, event.Object))
        m_events.push_back(std::move(event));
}

void HostEngine::RecordItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept
{
    if (auto change = ReadContainerItems(m_mods, aObjectFormId, aItemFormId))
        m_events.push_back(std::move(*change));
}

void HostEngine::SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept
{
    // Goes out from the funnel, as any of the host's lock changes.
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(m_mods.GetGameId(acObject)));
    if (pObject && pObject->SetLockState(aLocked, aLockLevel))
        spdlog::info("HostEngine: took a guest's lock {:X} {} at {}", acObject.LogFormat(), aLocked ? "locked" : "unlocked", aLockLevel);
    else
        spdlog::warn("HostEngine: a guest's lock {:X}: no such object here", acObject.LogFormat());
}

void HostEngine::SetItems(const GameId& acObject, const GameId& acItem, uint32_t aCount) noexcept
{
    const uint32_t cObjectId = m_mods.GetGameId(acObject);
    const uint32_t cItemId = m_mods.GetGameId(acItem);
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (pObject && !Cast<Actor>(pObject) && pObject->SetContainerCount(Cast<TESBoundObject>(TESForm::GetById(cItemId)), static_cast<int32_t>(aCount)))
    {
        spdlog::info("HostEngine: took a guest's items {:X} holds {} of {:X}", acObject.LogFormat(), aCount, acItem.LogFormat());
        RecordItems(cObjectId, cItemId);
    }
    else
        spdlog::warn("HostEngine: a guest's items {:X} holds {} of {:X}: no such container or item here", acObject.LogFormat(), aCount, acItem.LogFormat());
}

TiltedPhoques::Vector<ObjectChange> HostEngine::TakeObjectChanges() noexcept
{
    auto [created, changed, removed] = s_log.Take();
    TiltedPhoques::Vector<ObjectChange> changes;
    // Created first: the changes after refer to them.
    for (const uint32_t cFormId : created)
    {
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        if (auto create = pObject ? ReadCreate(*pObject, m_mods) : std::nullopt)
        {
            m_created.insert(cFormId);
            m_rests.Forget(cFormId); // an id used before: the old one's rest is not this one's
            changes.push_back(std::move(*create));
        }
    }
    // Then the states, each as it is now.
    for (const auto& [cFormId, cFlags] : changed)
    {
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        GameId id;
        if (!pObject || !IsShared(cFormId) || !m_mods.GetServerModId(cFormId, id))
            continue;
        // A physics move goes out once it rests (m_rests): each game simulates the flight.
        ReferenceStates::Read(*pObject, id, cFlags & ~TESObjectREFR::CHANGE_REFR_HAVOK_MOVE, m_mods, changes);
    }
    for (const uint32_t cFormId : m_rests.TakeRested())
        if (const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId)))
            if (GameId id; IsShared(cFormId) && m_mods.GetServerModId(cFormId, id))
                ReferenceStates::Read(*pObject, id, TESObjectREFR::CHANGE_REFR_MOVE, m_mods, changes);
    // Records that lost rows: the guests drop theirs too.
    for (const uint32_t cFormId : removed)
        if (const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId)))
            if (GameId id; IsShared(cFormId) && m_mods.GetServerModId(cFormId, id))
                changes.push_back(ReadRecord(*pObject, id));
    // A deleted created one's id is the game's to use again.
    for (const auto& [cFormId, cFlags] : changed)
        if (const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId)); m_created.count(cFormId) && (!pObject || pObject->IsDeleted()))
            m_created.erase(cFormId);
    // Events last: an object a script enabled and then animated is enabled for the animation.
    changes.insert(changes.end(), std::make_move_iterator(m_events.begin()), std::make_move_iterator(m_events.end()));
    m_events.clear();
    return changes;
}

bool HostEngine::IsShared(uint32_t aFormId) const noexcept
{
    return aFormId < 0xFF000000 || m_created.count(aFormId);
}

std::optional<TiltedPhoques::Vector<ObjectChange>> HostEngine::ReadCellState(const GameId& acCell,
                                                                             const TiltedPhoques::Vector<ChangedReference>& acChanged) const noexcept
{
    auto* pCell = Cast<TESObjectCELL>(TESForm::GetById(m_mods.GetGameId(acCell)));
    if (!pCell || !pCell->IsFullyLoaded())
        return std::nullopt;
    TiltedPhoques::Map<uint32_t, uint32_t> guestRows;
    for (const auto& cRef : acChanged)
        if (const uint32_t cFormId = m_mods.GetGameId(cRef.Id))
            guestRows[cFormId] |= cRef.Flags;
    TiltedPhoques::Vector<ObjectChange> changes;
    const auto cReferences = pCell->GetReferences();
    // The host's created ones there first, so the guest makes its own, with their rows.
    for (const TESObjectREFR* pRef : cReferences)
    {
        if (!pRef->IsTemporary() || !IsShared(pRef->formID))
            continue;
        GameId id;
        auto create = ReadCreate(*pRef, m_mods);
        if (create && m_mods.GetServerModId(pRef->formID, id))
        {
            changes.push_back(std::move(*create));
            ReferenceStates::Read(*pRef, id, ReferenceStates::RecordedRows(*pRef), m_mods, changes);
        }
    }
    // Then the rows this game or the guest changed, and the host's record of them.
    for (const TESObjectREFR* pRef : cReferences)
    {
        const auto cGuest = guestRows.find(pRef->formID);
        const bool cGuestChanged = cGuest != guestRows.end() && pRef->formType == FormType::Reference;
        const uint32_t cRows = ReferenceStates::ChangedRows(*pRef) | (cGuestChanged ? cGuest->second & ReferenceStates::RowFlags() : 0);
        GameId id;
        if (!cRows || !m_mods.GetServerModId(pRef->formID, id))
            continue;
        ReferenceStates::Read(*pRef, id, cRows, m_mods, changes);
        changes.push_back(ReadRecord(*pRef, id));
    }
    // Containers last: the host's roll of their leveled entries, then all they hold.
    for (TESObjectREFR* pRef : cReferences)
    {
        if (pRef->formType != FormType::Reference || pRef->IsTemporary() || !pRef->GetContainer())
            continue;
        pRef->InitInventoryIfRequired();
        if (auto contents = ReadContainerContents(m_mods, *pRef))
            changes.push_back(std::move(*contents));
    }
    return changes;
}
