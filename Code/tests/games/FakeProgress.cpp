#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/DeathActions.h>
#include <Events/HitEvent.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Events/CallEndedEvent.h>
#include <Events/RoutedCallEvent.h>
#include <Events/PlayerProgressEvent.h>
#include <Games/EngineNames.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/SystemQuests.h>
#include <Events/SystemQuestLearnedEvent.h>
#include <ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <Games/Overrides.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Services/SaveService.h>
#include <Wire/Buffer.h>
#include <World.h>
#include <FormIds.h>

using namespace PartyRules;

namespace
{
// The skills' actor values from 6 on, as Papyrus names them (ActorValueInfo).
constexpr std::array<std::string_view, 18> kSkills{
    "OneHanded",  "TwoHanded",  "Marksman",    "Block",    "Smithing",    "HeavyArmor",
    "LightArmor", "Pickpocket", "Lockpicking", "Sneak",    "Alchemy",     "Speechcraft",
    "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"};
constexpr std::array<std::pair<std::string_view, int32_t>, 4> kStats{{{"Health", 24}, {"Magicka", 25}, {"Stamina", 26}, {"CarryWeight", 32}}};

template <class T> T ArgumentOf(const LocalCall& acCall, size_t aIndex, T aDefault = {})
{
    const auto* pValue = aIndex < acCall.Arguments.size() ? std::get_if<T>(&acCall.Arguments[aIndex]) : nullptr;
    return pValue ? *pValue : aDefault;
}

int32_t SkillOf(const LocalCall& acCall)
{
    const auto cName = ArgumentOf<std::string>(acCall, 0);
    const auto cIt = std::ranges::find_if(kSkills, [&](std::string_view aSkill) { return SameName(cName, aSkill); });
    return cIt == kSkills.end() ? -1 : 6 + static_cast<int32_t>(cIt - kSkills.begin());
}
} // namespace

void FakeEngine::TakeProgress(const LocalCall& acCall, uint32_t aActor)
{
    const auto cIs = [&](std::string_view aFunction) { return SameName(acCall.Function, aFunction); };
    const uint32_t cForm = ArgumentOf<LocalForm>(acCall, 0).FormId;
    const bool cPlayer = aActor == kPlayer;
    if (cIs("TeachWord") || cIs("UnlockWord"))
    {
        if (cIs("TeachWord"))
            ++WordsLearnedShown;
        WordsKnown.insert(cForm);
        if (cIs("UnlockWord"))
            WordsUnlocked.insert(cForm);
        return;
    }
    if (cIs("SetRace"))
    {
        if (cPlayer)
            PlayerSwitchesRace(cForm);
        else
            Npcs[Actors.at(aActor).Base].Race = cForm;
        return;
    }
    if (cIs("AddToFaction") || cIs("SetFactionRank"))
        Factions[aActor][cForm] = ArgumentOf<int32_t>(acCall, 1);
    else if (cIs("RemoveFromFaction"))
        Factions[aActor].erase(cForm);
    else if (cIs("AddSpell"))
        AddedSpells[aActor].insert(cForm);
    else if (cIs("RemoveSpell"))
        AddedSpells[aActor].erase(cForm);
    else if (cIs("ModActorValue") && SameName(ArgumentOf<std::string>(acCall, 0), "DragonSouls"))
    {
        const float cAmount = ArgumentOf<float>(acCall, 1);
        Souls[aActor] += cAmount;
        if (cPlayer && cAmount > 0.f)
            ++SoulsAbsorbedShown;
    }
    else if (cPlayer)
    {
        RewardCalls.push_back(acCall);
        if (cIs("AdvanceSkill"))
            RewardExperience[SkillOf(acCall)] += ArgumentOf<float>(acCall, 1) * RewardMultiplier;
        else if (cIs("IncrementSkill") || cIs("IncrementSkillBy"))
            RewardLevels[SkillOf(acCall)] += cIs("IncrementSkill") ? 1u : static_cast<uint32_t>(ArgumentOf<int32_t>(acCall, 1));
        else if (cIs("AddPerkPoints"))
            RewardPoints += ArgumentOf<int32_t>(acCall, 0);
        else if (cIs("AddPerk"))
        {
            RewardPerks.insert(cForm);
            const auto cId = SessionIdOf(cForm);
            if (std::ranges::none_of(Player.Perks, [&](const auto& cPerk) { return cPerk.Id == cId; }))
                Player.Perks.push_back({cId, 1});
        }
        else if (cIs("RemovePerk"))
        {
            RewardPerks.erase(cForm);
            std::erase_if(Player.Perks, [&](const auto& cPerk) { return cPerk.Id == SessionIdOf(cForm); });
        }
        else if (cIs("ModActorValue"))
            for (const auto& [cName, cValue] : kStats)
                if (SameName(ArgumentOf<std::string>(acCall, 0), cName))
                    RewardStats[cValue] += ArgumentOf<float>(acCall, 1);
    }
}

void FakeEngine::TeachesWord(uint32_t aWord, uint32_t aCause)
{
    RunScript({.Class = "Game", .Function = "TeachWord", .Arguments = {LocalForm{aWord}}}, aCause);
}

void FakeEngine::UnlocksWord(uint32_t aWord, uint32_t aCause)
{
    RunScript({.Class = "Game", .Function = "UnlockWord", .Arguments = {LocalForm{aWord}}}, aCause);
}

// The engine's own, in the Magic menu: no script, so this player's own.
void FakeEngine::SpendsSoul(uint32_t aWord)
{
    if (Souls[kPlayer] < 1.f || !WordsKnown.contains(aWord) || !WordsUnlocked.insert(aWord).second)
        return;
    Souls[kPlayer] -= 1.f;
}

void FakeEngine::ModifiesSouls(uint32_t aActor, float aAmount, uint32_t aCause)
{
    RunScript({.Class = "Actor", .Function = "ModActorValue", .Self = LocalForm{aActor}, .Arguments = {std::string("DragonSouls"), aAmount}},
              aCause);
}

void FakeEngine::SetsFaction(uint32_t aActor, uint32_t aFaction, int32_t aRank, uint32_t aCause, bool aStory, uint32_t aQuest)
{
    LocalCall call{.Class = "Actor", .Function = aRank < 0 ? "RemoveFromFaction" : "SetFactionRank", .Self = LocalForm{aActor},
                   .Arguments = {LocalForm{aFaction}}};
    if (aRank >= 0)
        call.Arguments.emplace_back(aRank);
    RunScript(call, aCause, aStory, BodyQuests.contains(aQuest));
}

Perks FakeEngine::GetPlayerPerks() const noexcept
{
    Perks perks{Player.Perks};
    if (const auto it = AddedSpells.find(kPlayer); it != AddedSpells.end())
        for (const uint32_t cSpell : it->second)
            if (const auto cId = ToSession(cSpell))
                perks.Spells.push_back(*cId);
    return perks;
}

void FakeEngine::SetCopyPerks(uint32_t aFormId, const Perks& acPerks) noexcept
{
    ActorPerks[aFormId] = acPerks.Ranks;
    auto& spells = AddedSpells[aFormId];
    spells.clear();
    for (const GameId& cId : acPerks.Spells)
        spells.insert(ToGame(cId));
}

void FakeEngine::AddsSpell(uint32_t aActor, uint32_t aSpell, bool aAdded, uint32_t aCause, bool aStory, uint32_t aQuest)
{
    RunScript({.Class = "Actor", .Function = aAdded ? "AddSpell" : "RemoveSpell", .Self = LocalForm{aActor}, .Arguments = {LocalForm{aSpell}}},
              aCause, aStory, BodyQuests.contains(aQuest));
}

PlayerRecord FakeEngine::ReadPlayer() const noexcept
{
    PlayerRecord record = Player;
    if (record.Name.empty()) record.Name = Name;
    const Actor& cPlayer = Actors.at(kPlayer);
    // As JoinBridge: what the player carries, from its inventory; no place. Its souls and the words it unlocked.
    record.Items = cPlayer.Items;
    record.Recipes = World::Get().GetRecipes().Of(record.Items);
    std::erase_if(record.Values, [](const PlayerRecord::Value& acValue) { return acValue.Index == 133; });
    record.Values.push_back({133, Souls.contains(kPlayer) ? Souls.at(kPlayer) : 0.f});
    for (const auto& [cIndex, cValue] : RewardStats)
    {
        std::erase_if(record.PermanentValues, [&](const PlayerRecord::Value& acValue) { return acValue.Index == cIndex; });
        record.PermanentValues.push_back({static_cast<uint8_t>(cIndex), cValue});
    }
    record.Words.clear();
    for (const uint32_t cWord : WordsUnlocked)
        record.Words.push_back(GameId(0, cWord));
    if (const auto cSpells = AddedSpells.find(kPlayer); cSpells != AddedSpells.end())
        for (const uint32_t cSpell : cSpells->second)
            if (std::ranges::find(record.Spells, GameId(0, cSpell)) == record.Spells.end())
                record.Spells.push_back(GameId(0, cSpell));
    // As JoinBridge: its own system quests' instances.
    record.Extras.erase(SystemQuestState::kExtra);
    if (SystemQuestList.Any())
        record.Extras[SystemQuestState::kExtra] = ReadSystemQuests();
    return record;
}

void FakeEngine::ApplyPlayer(const PlayerRecord& acRecord) noexcept
{
    Actor& cPlayer = Actors.at(kPlayer);
    Player = acRecord;
    Player.Key.clear();
    Player.Items = {};
    for (const Recipe& cRecipe : acRecord.Recipes)
        World::Get().GetRecipes().Learn(cRecipe);
    cPlayer.Items = acRecord.Items;
    AddedSpells[kPlayer].clear();
    for (const auto& cSpell : acRecord.Spells)
        AddedSpells[kPlayer].insert(cSpell.BaseId);
    // As JoinBridge: its own souls and unlocked words, replacing the save's purchased flags with no HUD.
    for (const PlayerRecord::Value& cValue : acRecord.Values)
        if (cValue.Index == 133)
            Souls[kPlayer] = cValue.Base;
    WordsUnlocked.clear();
    for (const GameId& cWord : acRecord.Words)
        WordsUnlocked.insert(cWord.BaseId);
    RewardStats.clear();
    for (const PlayerRecord::Value& cValue : acRecord.PermanentValues)
        RewardStats[cValue.Index] = cValue.Base;
    Player.Words.clear();
    // As JoinBridge: at the record's place (kept in the host's save), else where the save put it, the host's spot.
    if (acRecord.HasPlace())
    {
        cPlayer.Cell = acRecord.Cell.BaseId;
        std::ranges::copy(acRecord.Position, cPlayer.Position);
    }
    Player.Cell = {};
    Player.WorldSpace = {};
    Player.Position = {};
    const auto cQuests = acRecord.Extras.find(SystemQuestState::kExtra);
    ApplySystemQuests(cQuests != acRecord.Extras.end() ? cQuests->second : std::string{});
    Player.Extras.erase(SystemQuestState::kExtra);
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::SaveNow(const std::string& acName) noexcept
{
    if (MadeSave)
        return *std::exchange(MadeSave, std::nullopt);
    if (!PendingSave)
        PendingSave = acName;
    return std::nullopt;
}

bool FakeEngine::WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept
{
    PendingLoad = {acName, acFiles};
    return true;
}

// The save's game file is the player's record; its co-save the records SaveService writes.
