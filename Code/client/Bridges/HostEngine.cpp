#include <Bridges/HostEngine.h>

#include <Breadcrumbs.h>
#include <Systems/ModSystem.h>

#include <Forms/TESGlobal.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWeather.h>
#include <Games/TES.h>
#include <Sky/Sky.h>
#include <TESObjectREFR.h>
#include <TimeManager.h>

#include <cmath>

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
} // namespace

HostEngine::HostEngine(const ModSystem& acMods) noexcept
    : m_mods(acMods)
{
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
