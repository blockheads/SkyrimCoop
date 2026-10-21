#include <Bridges/HostEngine.h>

#include <Bridges/ContainerItems.h>
#include <ContainerEvents.h>

#include <Breadcrumbs.h>
#include <Systems/ModSystem.h>

#include <Forms/FormChanges.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWeather.h>
#include <Games/ReferenceStates.h>
#include <Games/TES.h>
#include <Sky/Sky.h>
#include <Actor.h>
#include <TESObjectREFR.h>
#include <TimeManager.h>

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

// The references the funnel saw change, each once with its flags merged (each state goes as it is when taken, so
// their order does not matter). Filled on the engine's threads; the lock is a leaf (nothing is taken under it).
struct ChangedReferences
{
    void Add(uint32_t aFormId, uint32_t aFlags) noexcept
    {
        std::scoped_lock _(m_lock);
        m_changed[aFormId] |= aFlags;
    }

    TiltedPhoques::Map<uint32_t, uint32_t> Take() noexcept
    {
        std::scoped_lock _(m_lock);
        return std::exchange(m_changed, {});
    }

private:
    std::mutex m_lock;
    TiltedPhoques::Map<uint32_t, uint32_t> m_changed; // form id: change flags
};

// One for the funnel's watcher, a plain function: only one HostEngine lives at a time.
ChangedReferences s_changed;

// A world reference changed live: not an actor (its owner's), not one the game created (an id of its own), not
// by a load. Its flags worth sending (ReferenceStates).
void OnFormChange(const TESForm& acForm, uint32_t aFlags, bool aForced, bool aRecorded) noexcept
{
    const uint32_t cFlags = aFlags & ReferenceStates::SentFlags();
    if (cFlags && aRecorded && !aForced && acForm.formType == FormType::Reference && acForm.formID < 0xFF000000)
        s_changed.Add(acForm.formID, cFlags);
}
} // namespace

HostEngine::HostEngine(const ModSystem& acMods) noexcept
    : m_mods(acMods)
{
    if (!FormChanges::Watch(&OnFormChange))
        spdlog::error("{}: no slot left to watch the change funnel, the references' state stays here", __FUNCTION__);
    ContainerEvents::Start();
}

HostEngine::~HostEngine() noexcept
{
    FormChanges::Unwatch(&OnFormChange);
    s_changed.Take();
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
    // State first, then the events: an object a script enabled and then animated is enabled for the animation.
    TiltedPhoques::Vector<ObjectChange> changes;
    for (const auto& [cFormId, cFlags] : s_changed.Take())
    {
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        GameId id;
        if (pObject && m_mods.GetServerModId(cFormId, id))
            ReferenceStates::Read(*pObject, id, cFlags, m_mods, changes);
    }
    changes.insert(changes.end(), std::make_move_iterator(m_events.begin()), std::make_move_iterator(m_events.end()));
    m_events.clear();
    return changes;
}

std::optional<TiltedPhoques::Vector<ObjectChange>> HostEngine::ReadCellState(const GameId& acCell,
                                                                             const TiltedPhoques::Vector<ChangedReference>& acChanged) const noexcept
{
    auto* pCell = Cast<TESObjectCELL>(TESForm::GetById(m_mods.GetGameId(acCell)));
    if (!pCell || !pCell->IsValid())
        return std::nullopt;
    TiltedPhoques::Map<uint32_t, uint32_t> guestRows;
    for (const auto& cRef : acChanged)
        if (const uint32_t cFormId = m_mods.GetGameId(cRef.Id))
            guestRows[cFormId] |= cRef.Flags;
    TiltedPhoques::Vector<ObjectChange> changes;
    const auto cReferences = pCell->GetReferences();
    for (const TESObjectREFR* pRef : cReferences)
    {
        uint32_t rows = ReferenceStates::ChangedRows(*pRef);
        if (const auto it = guestRows.find(pRef->formID); it != guestRows.end() && pRef->formType == FormType::Reference)
            rows |= it->second & ReferenceStates::RowFlags();
        GameId id;
        if (rows && m_mods.GetServerModId(pRef->formID, id))
            ReferenceStates::Read(*pRef, id, rows, m_mods, changes);
    }
    // Containers last: the host's roll of their leveled entries, then all they hold.
    for (TESObjectREFR* pRef : cReferences)
    {
        if (pRef->formType != FormType::Reference || pRef->formID >= 0xFF000000 || !pRef->GetContainer())
            continue;
        pRef->InitInventoryIfRequired();
        if (auto contents = ReadContainerContents(m_mods, *pRef))
            changes.push_back(std::move(*contents));
    }
    return changes;
}
