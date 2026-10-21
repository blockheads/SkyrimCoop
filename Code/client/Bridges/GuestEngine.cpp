#include <Bridges/GuestEngine.h>

#include <Breadcrumbs.h>
#include <Systems/ModSystem.h>

#include <Forms/TESGlobal.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWeather.h>
#include <Misc/BGSStoryManager.h>
#include <Papyrus/WorldEvents.h>
#include <Sky/Sky.h>
#include <TESObjectREFR.h>
#include <TimeManager.h>

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

void ApplyAliases(TESQuest& aQuest, const QuestState& acQuest, const ModSystem& acMods) noexcept
{
    // Stopped on the host: its aliases are cleared there (25049), so none are sent.
    if (acQuest.Aliases.empty() && !(acQuest.Flags & kStartedQuestFlags))
        for (BGSBaseAlias* pAlias : aQuest.aliases)
            if (pAlias && aQuest.GetRefAlias(pAlias->aliasId) && aQuest.GetAliasedRef(pAlias->aliasId))
                aQuest.ClearRefAlias(pAlias);

    for (const auto& cFill : acQuest.Aliases)
    {
        BGSBaseAlias* pAlias = aQuest.GetRefAlias(cFill.AliasId);
        if (!pAlias || cFill.Created)
            continue;
        TESObjectREFR* pWanted = cFill.Ref ? Cast<TESObjectREFR>(TESForm::GetById(acMods.GetGameId(cFill.Ref))) : nullptr;
        if (aQuest.GetAliasedRef(cFill.AliasId) == pWanted)
            continue;
        if (pWanted)
            aQuest.ForceRefIntoAlias(cFill.AliasId, pWanted);
        else
            aQuest.ClearRefAlias(pAlias);
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
} // namespace

GuestEngine::GuestEngine(const ModSystem& acMods) noexcept
    : m_mods(acMods)
{
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
    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(m_mods.GetGameId(acWeather)));
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

bool GuestEngine::SetGlobal(const GlobalValue& acGlobal) noexcept
{
    auto* pGlobal = Cast<TESGlobal>(TESForm::GetById(m_mods.GetGameId(acGlobal.Id)));
    if (!pGlobal || pGlobal->IsConstant())
        return false;
    pGlobal->f = acGlobal.Value;
    return true;
}

void GuestEngine::ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept
{
    auto* pQuest = Cast<TESQuest>(TESForm::GetById(m_mods.GetGameId(acQuest.Id)));
    if (!pQuest)
        return;
    TESQuest& quest = *pQuest;

    // Running or starting first, without this game's own start: no alias rolls, no start-up stage of its own.
    if (acQuest.Flags & kStartedQuestFlags)
        SetQuestFlags(quest, kStartedQuestFlags, acQuest.Flags);
    ApplyAliases(quest, acQuest, m_mods);
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
}
