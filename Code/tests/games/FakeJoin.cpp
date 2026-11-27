// FakeEngine: saves, loads and joining (IJoinBridge).
#include <CopyCombat.h>
#include <magic/InterruptCastEvent.h>
#include <CheckedPlacement.h>
#include "FakeEngine.h"
#include <magic/OwnerEffects.h>
#include <join/SaveWrittenEvent.h>
#include <set>
#include <span>
#include <combat/AddTargetEvent.h>
#include <magic/SummonEvent.h>
#include <actorstate/AiStateChangeEvent.h>
#include <magic/EffectVisualsChangeEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <combat/HitEvent.h>
#include <magic/EffectRules.h>
#include <magic/SpellCastEvent.h>
#include <travel/RemoveWaypointEvent.h>
#include <time/TimeScaleEvent.h>
#include <scripts/SystemQuests.h>
#include <ranges>
#include <join/GameLoadedEvent.h>
#include <join/SaveService.h>
#include <join/SaveNames.h>
#include <Wire/Buffer.h>
#include <core/World.h>

using namespace PartyRules;

std::string FakeEngine::PlayerId() noexcept
{
    return std::format("id:{:016x}", std::hash<std::string>{}(Name));
}

IJoinBridge::SaveFiles FakeEngine::MakeSave()
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    // The save holds where its player stands.
    PlayerRecord player = ReadPlayer();
    player.Knowledge = Knowledge;
    const Actor& cPlayer = Actors.at(kPlayer);
    player.Cell = GameId(0, cPlayer.Cell);
    player.Position = {cPlayer.Position[0], cPlayer.Position[1], cPlayer.Position[2]};
    player.Serialize(writer);
    Serialization::WriteVarInt(writer, References.size());
    // Created ones keep their ids in a save (measured, summons.md): their base, cell and state with them.
    for (const auto& [id, r] : References)
    {
        Serialization::WriteVarInt(writer, id);
        Serialization::WriteBool(writer, r.Locked);
        Serialization::WriteVarInt(writer, r.Base);
        Serialization::WriteVarInt(writer, r.Cell);
        Serialization::WriteBool(writer, r.Disabled);
    }
    // The words' flags (the world's), the player's own factions (its base's, kept with the save).
    for (const auto* pWords : {&WordsKnown, &WordsUnlocked})
    {
        Serialization::WriteVarInt(writer, pWords->size());
        for (const uint32_t cWord : *pWords)
            Serialization::WriteVarInt(writer, cWord);
    }
    const auto& cFactions = Factions[kPlayer];
    Serialization::WriteVarInt(writer, cFactions.size());
    for (const auto& [cFaction, cRank] : cFactions)
    {
        Serialization::WriteVarInt(writer, cFaction);
        Serialization::WriteVarInt(writer, static_cast<uint32_t>(cRank));
    }
    Own.Serialize(writer);
    return SaveFiles{std::string(reinterpret_cast<const char*>(buffer.GetData()), writer.Size()) + SavePadding, World::Get().ctx().at<SaveService>().WriteCoSave()};
}

void FakeEngine::RunMainLoop() noexcept
{
    RunNativeCombat();
    for (auto& [name, files] : std::exchange(SaveThread, {}))
    {
        WritesSaveFile(name, std::move(files));
        World::Get().GetRunner().Trigger(SaveWrittenEvent{name});
    }
    if (const auto cName = std::exchange(PendingSave, std::nullopt))
    {
        if (RefusesSaves || !World::Get().ctx().at<SaveService>().OnSaveRequested())
            MadeSave = std::optional<SaveFiles>{};
        else
        {
            Saves.push_back(*cName);
            MadeSave = MakeSave();
            WritesSaveFile(*cName, **MadeSave);
            // A menu or console save's file is written inside Save_Impl.
            World::Get().GetRunner().Trigger(SaveWrittenEvent{*cName});
            // JoinBridge::MakeSave: a join's image is read back, then deleted from the Saves folder.
            if (*cName == SaveNames::kTransfer)
                DeleteSave(*cName);
        }
    }
    const auto cLoad = std::exchange(PendingLoad, std::nullopt);
    if (!cLoad)
        return;
    const auto& [cName, cFiles] = *cLoad;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(cFiles.Game.data()), cFiles.Game.size()));
    PlayerRecord saved;
    saved.Deserialize(reader);
    const auto count = Serialization::ReadVarInt(reader);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t id = Serialization::ReadVarInt(reader);
        auto& reference = References[id];
        reference.Locked = Serialization::ReadBool(reader);
        reference.Base = static_cast<uint32_t>(Serialization::ReadVarInt(reader));
        reference.Cell = static_cast<uint32_t>(Serialization::ReadVarInt(reader));
        reference.Disabled = Serialization::ReadBool(reader);
    }
    for (auto* pWords : {&WordsKnown, &WordsUnlocked})
    {
        pWords->clear();
        const auto cWords = Serialization::ReadVarInt(reader);
        for (size_t i = 0; i < cWords; ++i)
            pWords->insert(static_cast<uint32_t>(Serialization::ReadVarInt(reader)));
    }
    auto& factions = Factions[kPlayer];
    factions.clear();
    const auto cFactions = Serialization::ReadVarInt(reader);
    for (size_t i = 0; i < cFactions; ++i)
    {
        const auto cFaction = static_cast<uint32_t>(Serialization::ReadVarInt(reader));
        factions[cFaction] = static_cast<int32_t>(Serialization::ReadVarInt(reader));
    }
    // The save's player's own state (save global 109, its stacks' hotkeys, effects, ...): the saving game's player's.
    Own.Deserialize(reader);
    // The save's player's souls: its value in the save (the record sets the joining character's own).
    Souls[kPlayer] = 0.f;
    for (const PlayerRecord::Value& cValue : saved.Values)
        if (cValue.Index == 133)
            Souls[kPlayer] = cValue.Base;
    // The save's system quests: its list (the co-save's) and their instances, the saving game's player's (the VM's).
    SystemQuestList.Clear();
    QuestVariables.clear();
    QuestTimers.clear();
    if (const auto cQuests = saved.Extras.find(SystemQuestState::kExtra); cQuests != saved.Extras.end())
        if (const auto cState = SystemQuestState::Decode(cQuests->second))
        {
            for (const SystemQuestState::Quest& cQuest : cState->Quests)
                SystemQuests::Learn(cQuest.Id.BaseId, false);
            ApplySystemQuests(cQuests->second);
        }
    saved.Extras.erase(SystemQuestState::kExtra);
    Player = saved;
    Player.Own = {};
    Knowledge = saved.Knowledge;
    Player.Items = {};
    Player.Cell = {};
    Player.Position = {};
    Actor& player = Actors.at(kPlayer);
    player.Bleeding = player.Dead = false;
    player.DyingFrames = player.DeadFrames = 0;
    // A load's new 3D, its roots set by PlayerCharacter::LoadGame's 40476.
    player.BodyHidden = false;
    player.FirstRootHidden = true;
    ShowFirstPersonRoot(CameraFirstPerson);
    player.Values = FullValues();
    player.Items = saved.Items;
    player.Cell = saved.Cell.BaseId;
    std::copy(saved.Position.begin(), saved.Position.end(), player.Position);
    LoadCell(player.Cell);
    auto& saves = World::Get().ctx().at<SaveService>();
    saves.RevertCoSave();
    saves.ReadCoSave(cFiles.CoSave, CharacterRecords::kVersion);
    AtMainMenu = false;
    CharacterId = SaveNames::CharacterId(cName);
    Loaded.push_back(cName);
    LoadedFiles = cFiles;
    World::Get().GetRunner().Trigger(GameLoadedEvent{});
}

bool FakeEngine::Autosaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested(true))
        return false;
    Saves.push_back(acName);
    return true;
}

bool FakeEngine::Quicksaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested())
        return false;
    Saves.push_back(acName);
    SaveThread.emplace_back(acName, MakeSave());
    return true;
}

void FakeEngine::OffThread(std::function<std::function<void()>()> aWork) noexcept
{
    if (DefersWork)
        return QueuedWork.push_back(std::move(aWork));
    if (const auto cDone = aWork())
        cDone();
}

void FakeEngine::FinishWork() noexcept
{
    for (const auto& cWork : std::exchange(QueuedWork, {}))
        if (const auto cDone = cWork())
            cDone();
}

std::vector<std::string> FakeEngine::HeldSaves() noexcept
{
    std::vector<std::string> ids;
    for (const auto& [cId, cData] : HeldSaveFiles)
        ids.push_back(cId);
    return ids;
}

std::optional<std::string> FakeEngine::ReadHeldSave(const std::string& acId) noexcept
{
    const auto it = std::ranges::find(HeldSaveFiles, acId, [](const auto& acFile) { return acFile.first; });
    return it != HeldSaveFiles.end() ? std::optional(it->second) : std::nullopt;
}

bool FakeEngine::WriteHeldSave(const std::string& acId, const std::string& acData) noexcept
{
    DropHeldSave(acId);
    HeldSaveFiles.emplace(HeldSaveFiles.begin(), acId, acData);
    return true;
}

void FakeEngine::DropHeldSave(const std::string& acId) noexcept
{
    std::erase_if(HeldSaveFiles, [&](const auto& acFile) { return acFile.first == acId; });
}

// As Save_Impl's hook: SaveService decides first.
bool FakeEngine::PlayerSaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested())
        return false;
    Saves.push_back(acName);
    return true;
}

// From the map ("Remove It"): the only removal our hook raises.
void FakeEngine::RemovesMarker()
{
    World::Get().GetRunner().Trigger(RemoveWaypointEvent());
    Waypoint.reset();
}

// HookSetGlobalTimeMultiplier: a change of this game's own, raised while connected.
void FakeEngine::SlowsTime(float aMultiplier)
{
    if (aMultiplier != WantedTimeScale && World::Get().GetTransport().IsConnected())
        World::Get().GetRunner().Trigger(TimeScaleEvent{aMultiplier});
    WantedTimeScale = aMultiplier;
}

void FakeEngine::CastsSpell(uint32_t aCaster, uint32_t aSpell, uint32_t aSource, uint32_t aTarget)
{
    const auto cActor = Actors.find(aCaster);
    if (cActor == Actors.end())
        return;
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(SpellCastEvent(aCaster, aSpell, aTarget, aSource, false, cActor->second.Loaded));
    Casts.push_back({aCaster, aSpell, aSource, aTarget});
    if (Spell(aSpell).Concentration)
        Channels[{aCaster, aSource}] = aSpell;
}

void FakeEngine::StopsCasting(uint32_t aCaster, uint32_t aSource)
{
    const auto cActor = Actors.find(aCaster);
    if (cActor == Actors.end() || !Channels.erase({aCaster, aSource}))
        return;
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(InterruptCastEvent(aCaster, aSource));
}

// HookAddTarget: reads the landing, EffectRules::Of decides.
void FakeEngine::SpellHits(uint32_t aTarget, uint32_t aCaster, uint32_t aSpell, uint32_t aEffect, float aMagnitude,
                           bool aDualCast)
{
    if (!Actors.contains(aTarget))
        return;
    const SpellForm cSpell = Spell(aSpell);
    const auto cVerdict = EffectRules::Of({
        .Target = PartyOf(aTarget),
        .Caster = PartyOf(aCaster),
        .TargetIsThisPlayer = aTarget == kPlayer,
        .CasterIsTarget = aCaster == aTarget,
        .Guest = HoldSession != 0,
        .Hosting = World::Get().IsHosting(),
        .Summon = cSpell.Summons != 0,
        .Shared = !cSpell.Summons && !cSpell.Invisibility,
        .Healing = cSpell.Healing,
        .Buff = cSpell.Buff,
        .WorldImpact = InWorldImpact,
    });
    if (cVerdict.AskOwner)
    {
        AddTargetEvent event;
        event.TargetID = aTarget;
        event.CasterID = Actors.contains(aCaster) ? aCaster : 0; // an actor's only
        event.SpellID = aSpell;
        event.EffectID = aEffect;
        event.Magnitude = aMagnitude;
        event.IsDualCasting = aDualCast;
        World::Get().GetRunner().Trigger(event);
    }
    if (cVerdict.Hit)
        World::Get().GetRunner().Trigger(HitEvent(aCaster, aTarget));
    if (!cVerdict.Apply)
        return;
    if (cVerdict.Summon)
        World::Get().GetRunner().Trigger(SummonEvent{aSpell, aEffect, aMagnitude});
    // A summon on its caster makes its creature (a guest's CreateSummon makes nothing).
    if (cSpell.Summons && aTarget == aCaster)
        AddMagicTargetEffect(aTarget, aCaster, aSpell, aEffect, aMagnitude, false, false, false);
    else if (EffectHolds(aEffect, aDualCast, aCaster))
        StartsEffect({aTarget, aCaster, aSpell, aEffect});
}

void FakeEngine::ExplosionHits(uint32_t aTarget, uint32_t aExplosion, uint32_t aSpell, uint32_t aEffect, float aMagnitude)
{
    // Its enchantment's AddTarget sees no caster actor (43849, by its NonActorMagicCaster): a world cast.
    SpellHits(aTarget, 0, aSpell, aEffect, aMagnitude);
    (void)aExplosion;
}

void FakeEngine::HazardHits(uint32_t aTarget, uint32_t aSpell, uint32_t aEffect, float aMagnitude)
{
    // HazardTargets__CheckActor (43987): only an actor not under its spell (IsSpellTarget; a copy answers its owner's).
    if (!ActorHasEffect(aTarget, aEffect))
        SpellHits(aTarget, 0, aSpell, aEffect, aMagnitude);
}

void FakeEngine::StartsEffect(const Effect& acEffect)
{
    Effects.push_back(acEffect);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{acEffect.Target, 0});
}

bool FakeEngine::ActorHasEffect(uint32_t aActor, uint32_t aEffect) const
{
    const auto cActor = Actors.find(aActor);
    if (cActor != Actors.end() && cActor->second.Copy)
        return OwnerEffects::Has(cActor->second.OwnerEffects, aEffect);
    return std::ranges::any_of(Effects, [&](const Effect& acEffect) { return acEffect.Target == aActor && acEffect.EffectId == aEffect; });
}

bool FakeEngine::EffectHolds(uint32_t aEffect, bool aDualCast, uint32_t aCaster) const noexcept
{
    const auto cCondition = EffectConditions.find(aEffect);
    if (cCondition == EffectConditions.end())
        return true;
    if (cCondition->second.OnCaster)
        return aCaster != 0;
    if (cCondition->second.DualCast)
        return aDualCast;
    const auto cGlobal = Globals.find(cCondition->second.Global);
    return (cGlobal != Globals.end() ? cGlobal->second : 0.f) == cCondition->second.Equals;
}

uint32_t FakeEngine::MakesSummon(uint32_t aCaster, uint32_t aBase)
{
    const auto cCaster = Actors.find(aCaster);
    if (cCaster == Actors.end())
        return 0;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Commander == aCaster && !cActor.Copy)
        {
            DeleteActor(cId);
            break;
        }
    const auto& cPosition = cCaster->second.Position;
    const uint32_t cId = LoadNpc(aBase, cCaster->second.Cell, {cPosition[0] + 100.f, cPosition[1], cPosition[2]});
    Actors.at(cId).Commander = aCaster;
    if (Attached.contains(Actors.at(cId).Cell))
        Load3D(cId);
    return cId;
}

void FakeEngine::Fights(uint32_t aActor, uint32_t aTarget)
{
    auto& ai = Actors.at(aActor).Ai;
    ai.CombatState = aTarget ? 1 : 0;
    ai.CombatTargetId = aTarget;
    auto& actor = Actors.at(aActor);
    actor.GroupTargets = aTarget ? std::vector{aTarget} : std::vector<uint32_t>{};
    if (aTarget && !actor.GroupId)
        actor.GroupId = NextGroupId++;
    else if (!aTarget)
        actor.GroupId.reset();
    World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::GroupTargets(uint32_t aActor, std::vector<uint32_t> aTargets)
{
    auto& actor = Actors.at(aActor);
    if (aTargets.empty())
        actor.GroupId.reset();
    else if (!actor.GroupId)
        actor.GroupId = NextGroupId++;
    actor.GroupTargets = std::move(aTargets);
    World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

bool FakeEngine::PlayerFlaggedInCombat() const
{
    return std::ranges::any_of(Actors, [](const auto& acEntry) { return std::ranges::contains(acEntry.second.GroupTargets, kPlayer); });
}

std::vector<uint32_t> FakeEngine::CompassEnemies() const
{
    std::vector<uint32_t> enemies;
    for (const auto& [cId, cActor] : Actors)
        if (std::ranges::contains(cActor.GroupTargets, kPlayer))
            enemies.push_back(cId);
    return enemies;
}

void FakeEngine::AttackedBy(uint32_t aVictim, uint32_t aAttacker)
{
    // HookStartCombat's check (38561); its wiring is checked in Skyrim (scenario host-dies-guest-still-fought).
    auto& victim = Actors.at(aVictim);
    if (CopyCombat::MayStart(victim.Copy, false))
        victim.OwnCombatTarget = aAttacker;
}

void FakeEngine::FurnitureReceives(uint32_t aFurniture, const std::string& acEvent)
{
    FurnitureGraphEvents[aFurniture].push_back(acEvent);
    auto& state = FurnitureStates.try_emplace(aFurniture, "Idle").first->second;
    if (const auto it = FurnitureTransitions.find({state, acEvent}); it != FurnitureTransitions.end())
        state = it->second;
}

void FakeEngine::LoadsFurniture(uint32_t aFurniture)
{
    FurnitureUnloaded.erase(aFurniture);
    World::Get().GetRunner().Trigger(ReferenceLoadedEvent{aFurniture});
}

void FakeEngine::SitsIn(uint32_t aActor, uint32_t aFurniture)
{
    auto& actor = Actors.at(aActor);
    actor.State1 = (actor.State1 & ~(0xFu << kSitShift)) | uint32_t(aFurniture ? 3 : 0) << kSitShift;
    actor.InFurniture = aFurniture != 0;
    actor.LinkedFurniture = aFurniture;
    actor.Ai.FurnitureId = aFurniture;
    World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::LooksAt(uint32_t aActor, uint32_t aSlot, uint32_t aTarget)
{
    auto& slot = Actors.at(aActor).Ai.HeadTrackIds.at(aSlot);
    if (std::exchange(slot, aTarget) != aTarget)
        World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::Burns(uint32_t aActor, uint32_t aEffect, bool aOn)
{
    auto& visuals = Actors.at(aActor).Visuals;
    aOn ? (void)visuals.insert(aEffect) : (void)visuals.erase(aEffect);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aActor, 0});
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::ReadSave(const std::string& acName) noexcept
{
    const auto it = SavedFiles.find(acName);
    return it != SavedFiles.end() ? std::optional(it->second) : std::nullopt;
}

void FakeEngine::WritesSaveFile(const std::string& acName, SaveFiles aFiles)
{
    SavedFiles[acName] = std::move(aFiles);
    uint64_t newest = 0;
    for (const auto& [cName, cTime] : SaveTimes)
        newest = std::max(newest, cTime);
    SaveTimes[acName] = newest + 1;
}

void FakeEngine::DeleteSave(const std::string& acName) noexcept
{
    SavedFiles.erase(acName);
    SaveTimes.erase(acName);
}

bool FakeEngine::Continues()
{
    const std::string* pNewest = nullptr;
    uint64_t newest = 0;
    for (const auto& [cName, cFiles] : SavedFiles)
    {
        const auto cTime = SaveTimes.contains(cName) ? SaveTimes.at(cName) : 0;
        if (!pNewest || cTime > newest)
            pNewest = &cName, newest = cTime;
    }
    if (!AtMainMenu || !pNewest)
        return false;
    // 0xD0000100 hands the file to the save utility: no Load_Impl, so none of our load hooks.
    PendingLoad = {*pNewest, SavedFiles.at(*pNewest)};
    return true;
}

std::vector<std::string> FakeEngine::LoadList(std::optional<uint32_t> aPicked) const
{
    using enum SaveList::Limit;
    const uint32_t cFilter = SaveListLimit == kAll ? aPicked.value_or(CharacterId) : CharacterId;
    std::vector<std::pair<uint64_t, std::string>> listed;
    for (const auto& [cName, cFiles] : SavedFiles)
        if (SaveListLimit != kNothing && (cFilter == SaveList::kAllCharacters || SaveNames::CharacterId(cName) == cFilter))
            listed.emplace_back(SaveTimes.contains(cName) ? SaveTimes.at(cName) : 0, cName);
    std::ranges::sort(listed, std::greater{});
    std::vector<std::string> names;
    for (auto& [cTime, name] : listed)
        names.push_back(std::move(name));
    return names;
}

std::vector<uint32_t> FakeEngine::CharacterRows() const
{
    std::vector<uint32_t> rows{SaveList::kAllCharacters};
    for (const auto& [cName, cFiles] : SavedFiles)
        if (const uint32_t cId = SaveNames::CharacterId(cName); !std::ranges::contains(rows, cId))
            rows.push_back(cId);
    std::erase_if(rows, [&](uint32_t aRow) { return !SaveList::Shows(SaveListLimit, CharacterId, aRow); });
    return rows;
}

bool FakeEngine::PlayerLoads(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnLoadRequested(acName))
        return false;
    const auto cFiles = ReadSave(acName);
    return cFiles && WriteAndLoad(acName, *cFiles);
}

bool FakeEngine::PlaceBesideHost() noexcept
{
    ++PlacementChecks;
    auto& player = Actors.at(kPlayer);
    const glm::vec3 cAnchor(player.Position[0], player.Position[1], player.Position[2]);
    const auto cSpot = CheckedPlacement::Beside(cAnchor, [&](const glm::vec3& acFrom, const glm::vec3& acTo) {
        if (acTo.z < acFrom.z - 200.f)
            return std::optional{CheckedPlacement::Hit{true, {acFrom.x, acFrom.y, cAnchor.z}, {0.f, 0.f, 1.f}}};
        return std::optional{CheckedPlacement::Hit{!PlacementClear, acTo}};
    });
    if (!cSpot) return false;
    std::copy_n(&cSpot->x, 3, player.Position);
    return true;
}

void FakeEngine::StartsHitVisuals(uint32_t aActor, uint32_t aEffect, float aDuration, uint32_t aCastingType)
{
    const bool cFlash = HitVisualsRules::Flash(EffectVisualFlags[aEffect], true, aDuration, aCastingType);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aActor, cFlash ? aEffect : 0});
}
