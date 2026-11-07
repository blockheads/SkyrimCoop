#include <Games/Skyrim/CopyCombat.h>
#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include <Events/ActorRenamedEvent.h>
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Services/ContainerOpens.h>
#include <Services/DeathDrops.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorDamagedEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/DeathActions.h>
#include <Games/Animation/ReplayCapture.h>
#include <Games/Animation/ReplayGraph.h>
#include <Games/NpcSnapshot.h>
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
#include <Games/Animation/ReplayState.h>
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
bool FakeEngine::IsLyingInRagdoll(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    if (it->second.Copy)
        return RagdollPosed(aFormId);
    return it->second.KnockedDown || std::ranges::any_of(Bodies, [&](const auto& acBody) {
        return acBody.second.RagdollOf == aFormId && (acBody.second.Awake || it->second.Dead);
    });
}

bool FakeEngine::RagdollPosed(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !it->second.Copy)
        return false;
    return it->second.KnockedDown ||
           (it->second.Dead && std::ranges::any_of(Bodies, [&](const auto& acBody) { return acBody.second.RagdollOf == aFormId && acBody.second.Held; }));
}

void FakeEngine::ShovePlayer() noexcept
{
    auto& player = Actors.at(kPlayer);
    const auto cPasses = [this](uint32_t aFormId) { return std::ranges::find(PassingThrough, aFormId) != PassingThrough.end(); };
    for (const auto& [cId, cActor] : Actors)
    {
        if (!cActor.Copy || cActor.Player || !cActor.Stepped || cActor.Dead || cActor.Disabled || !cActor.Loaded || cActor.Cell != player.Cell)
            continue;
        if (std::abs(cActor.Position[2] - player.Position[2]) >= kCapsuleTop || (cPasses(cId) && cPasses(kPlayer)))
            continue;
        glm::vec2 off(player.Position[0] - cActor.Position[0], player.Position[1] - cActor.Position[1]);
        if (glm::length(off) >= 2 * kCapsuleRadius)
            continue;
        if (glm::length(off) < 1e-3f)
            off = {1.f, 0.f};
        off = glm::normalize(off) * (2 * kCapsuleRadius);
        player.Position[0] = cActor.Position[0] + off.x;
        player.Position[1] = cActor.Position[1] + off.y;
        ++PlayerShoves;
    }
}

void FakeEngine::Frame()
{
    // A pair's clip runs on its own: its blow lands at HitFrame, a transition can hold off pairedStop until then.
    for (auto& [cId, actor] : Actors)
        if (actor.PairBlowIn && --actor.PairBlowIn == 0 && Actors.contains(actor.PairVictim))
            Strikes(cId, actor.PairVictim, 1000.f);
    // Clips loop on each game's own clock; an event every N of them is the graph's, no action behind it.
    if (Loop)
        for (auto& [cId, actor] : Actors)
        {
            if (actor.GraphState != Loop->State && actor.GraphState != Loop->Other)
                continue;
            if (++actor.LoopFrames < Loop->Frames)
                continue;
            actor.LoopFrames = 0;
            if (++actor.LoopsSeen < actor.LoopsToSend)
                continue;
            actor.LoopsSeen = 0;
            actor.LoopsToSend = 1 + (DrawGraphRandom(actor) >> 13) % Loop->Most;
            actor.LoopDraws.push_back(actor.LoopsToSend);
            actor.GraphState = actor.GraphState == Loop->State ? Loop->Other : Loop->State;
        }
    for (auto it = ClipLoads.begin(); it != ClipLoads.end();)
    {
        if (it->Frames-- > 0)
        {
            ++it;
            continue;
        }
        if (const auto cActor = Actors.find(it->FormId); cActor != Actors.end())
            cActor->second.LoadedClips.insert(it->Event);
        it->Done->store(true);
        it = ClipLoads.erase(it);
    }
    if (Actors.contains(kPlayer))
        ShovePlayer();
    for (auto& [cId, actor] : Actors)
    {
        if (!actor.Copy)
            continue;
        if (actor.KnockIn && !--actor.KnockIn)
            actor.KnockedDown = true;
        for (const auto& cDrive : m_pendingRagdolls.Take(cId))
        {
            if (!actor.Dead && !actor.KnockedDown)
                continue;
            const auto it = Bodies.find(cDrive.Frame.Key);
            if (it == Bodies.end())
                continue;
            auto& body = it->second;
            body.Held = true;
            body.Awake = false;
            body.RagdollOf = cId;
            body.Flight.Body.Position = cDrive.Frame.Position;
            body.Flight.Body.Rotation = cDrive.Frame.Rotation;
            ++body.Drives;
        }
        // Drawn: moved since its graph update while its ragdoll poses it, its old pose shows at the new place.
        const float cTurn = std::abs(std::remainder(actor.Rotation[2] - actor.GraphPlace[3], 6.28318531f));
        const float cSnap = glm::distance(glm::vec3(actor.Position[0], actor.Position[1], actor.Position[2]),
                                          glm::vec3(actor.GraphPlace[0], actor.GraphPlace[1], actor.GraphPlace[2])) + cTurn * kSnapReach;
        if (cSnap > 0.5f && RagdollPosed(cId))
        {
            ++actor.Snaps;
            actor.WorstSnap = std::max(actor.WorstSnap, cSnap);
        }
        // Its graph update: lying in its ragdoll, the engine keeps it on its root body, turned with it (37996).
        if (RagdollPosed(cId))
            for (const auto& [cKey, cBody] : Bodies)
                if (cBody.RagdollOf == cId && cKey.Index == 0)
                {
                    std::copy_n(&cBody.Flight.Body.Position.x, 3, actor.Position);
                    actor.Rotation[2] = HeadingOf(cBody.Flight.Body.Rotation);
                }
        std::copy_n(actor.Position, 3, actor.GraphPlace);
        actor.GraphPlace[3] = actor.Rotation[2];
    }
    ShakeSeconds = std::max(0.f, ShakeSeconds - 1.f / 60);
    // Game time at the time scale for the real time since the last frame, as the engine's frame delta is.
    const auto cNow = std::chrono::steady_clock::now();
    if (m_lastFrame.time_since_epoch().count())
        Calendar.Time += Calendar.TimeScale * std::chrono::duration<float>(cNow - m_lastFrame).count() / 3600.f;
    m_lastFrame = cNow;
    if (Calendar.Time >= 24.f)
    {
        Calendar.Time -= 24.f;
        ++Calendar.Day;
        ++Calendar.DaysPassed;
    }
    if (auto& player = Actors.at(kPlayer); player.Dead)
    {
        if (player.DyingFrames && !--player.DyingFrames)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{kPlayer});
        if (++player.DeadFrames >= kDeathReloadFrames && !DeathReloadHeld)
            Reloaded = true;
    }
    RunQueued();
}

void FakeEngine::Queue(LocalCall aCall, uint32_t aCause)
{
    m_queued.emplace_back(std::move(aCall), aCause);
}

void FakeEngine::RunQueued()
{
    auto queued = std::exchange(m_queued, {});
    for (const auto& [cCall, cCause] : queued)
    {
        const auto* cpSelf = std::get_if<LocalForm>(&cCall.Self);
        if (cpSelf && QuestFunctions.contains({cpSelf->FormId, cCall.Function}))
            CallsQuest(cCall, cCause, false, false);
        else
            RunScript(cCall, cCause);
    }
}

void FakeEngine::CallsQuest(const LocalCall& acCall, uint32_t aCause, bool aStory, bool aFromBody)
{
    const auto* cpQuest = std::get_if<LocalForm>(&acCall.Self);
    const uint32_t cQuest = cpQuest ? cpQuest->FormId : 0;
    const auto it = QuestFunctions.find({cQuest, acCall.Function});
    if (it == QuestFunctions.end())
        return;
    const bool cOwnFunction = IsQuestScriptFunction(acCall.Class); // not Quest.SetStage's wrapper
    if (cOwnFunction && LearnsSystemQuest(cQuest, 0, aFromBody, m_inSystemQuest, false))
        SystemQuests::Learn(cQuest, true);
    if (!cOwnFunction || !SystemQuestList.Contains(cQuest))
    {
        it->second(*this, acCall, aCause);
        return;
    }
    const auto cIsGuestCharacter = [this](uint32_t aActor) {
        const auto cIt = Actors.find(aActor);
        return !IsGuest && cIt != Actors.end() && cIt->second.Copy && cIt->second.Player;
    };
    const Routing cRouting = InSession ? RouteOfSystemCall(aCause, aStory, cIsGuestCharacter(aCause), !IsGuest, false) : Routing{};
    if (cRouting.To == Destination::kCauseGame)
    {
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOwner, .GuestFormId = aCause, .Call = acCall});
        return;
    }
    if (cRouting.To == Destination::kEveryGame)
    {
        LocalCall call = acCall;
        MarkEachPlayer(call, cRouting.MarkFor);
        if (cRouting.ThisPlayer)
            PartyNatives::Run(OnThisPlayer(call), 0);
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOthers, .Call = std::move(call)});
        if (!cRouting.Original)
            return;
    }
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerProgressEvent{});
    const bool cOuter = std::exchange(m_inSystemQuest, true);
    it->second(*this, acCall, aCause);
    m_inSystemQuest = cOuter;
}

void FakeEngine::FiresQuestTimer(uint32_t aQuest, const std::string& acFunction)
{
    // Its own event: this game's player's (WorldEvents' gate delivers it as this player's).
    CallsQuest({.Class = "Quest", .Function = acFunction, .Self = LocalForm{aQuest}}, kPlayer);
}

std::string FakeEngine::ReadSystemQuests() const
{
    SystemQuestState state;
    for (const uint32_t cQuest : SystemQuestList.All())
    {
        SystemQuestState::Quest& quest = state.Quests.emplace_back(SystemQuestState::Quest{GameId(0, cQuest)});
        if (const auto cIt = QuestVariables.find(cQuest); cIt != QuestVariables.end())
            for (const auto& [cName, cValue] : cIt->second)
                quest.Variables.push_back({"Script", cName, Utils::ToScriptValue(cValue)});
        if (const auto cIt = QuestTimers.find(cQuest); cIt != QuestTimers.end())
            for (const auto& [cEvent, cTimer] : cIt->second)
                quest.Timers.push_back(cTimer);
    }
    for (const uint32_t cGlobal : SystemQuestList.Globals())
        state.Globals.push_back({GameId(0, cGlobal), Globals.contains(cGlobal) ? Globals.at(cGlobal) : 0.f});
    return state.Encode();
}

void FakeEngine::ApplySystemQuests(std::string_view aState)
{
    const auto cState = SystemQuestState::Decode(aState);
    for (const uint32_t cQuest : SystemQuestList.All())
    {
        // As SystemQuests::ApplyState: the save's instance (the host's player's) goes; this player's comes back, or
        // its scripts start over.
        QuestVariables.erase(cQuest);
        QuestTimers.erase(cQuest);
        const SystemQuestState::Quest* cpKept = cState ? cState->Find(GameId(0, cQuest)) : nullptr;
        if (!cpKept)
            continue;
        for (const SystemQuestState::Variable& cVariable : cpKept->Variables)
            if (const auto cValue = Utils::ToLocalValue(cVariable.Value))
                QuestVariables[cQuest][cVariable.Name] = *cValue;
        for (const SystemQuestState::Timer& cTimer : cpKept->Timers)
            QuestTimers[cQuest][cTimer.Native] = cTimer;
    }
    if (cState)
        for (const SystemQuestState::Global& cGlobal : cState->Globals)
            if (SystemQuestList.IsGlobal(cGlobal.Id.BaseId))
                Globals[cGlobal.Id.BaseId] = cGlobal.Value;
}

FakeEngine& FakeEngine::Current() noexcept
{
    return World::Get().ctx().at<FakeEngine&>();
}

GameId SessionIdOf(uint32_t aFormId) noexcept
{
    return GameId(aFormId >> 24, aFormId & 0x00FFFFFF);
}

uint32_t FormIdOf(const GameId& acId) noexcept
{
    return (acId.ModId << 24) | acId.BaseId;
}

// The engine's created potions (ItemIds): the fake's Brewed.
std::optional<Recipe> CreatedItems::RecipeOf(uint32_t aFormId) noexcept
{
    const auto& cBrewed = FakeEngine::Current().Brewed;
    const auto it = cBrewed.find(aFormId);
    return it != cBrewed.end() ? std::optional(it->second) : std::nullopt;
}

uint32_t CreatedItems::Make(const Recipe& acRecipe) noexcept
{
    return FakeEngine::Current().Brew(acRecipe);
}

uint32_t FakeEngine::Brew(const Recipe& acRecipe)
{
    for (const auto& [cId, cRecipe] : Brewed)
        if (cRecipe == acRecipe)
            return cId;
    const uint32_t cId = m_nextTemporary++;
    Brewed.emplace(cId, acRecipe);
    return cId;
}

uint32_t FakeEngine::PlayerBrews(const Recipe& acRecipe, int32_t aCount)
{
    const uint32_t cPotion = Brew(acRecipe);
    GetsItem(kPlayer, cPotion, aCount);
    return cPotion;
}

ActorValues FakeEngine::FullValues()
{
    ActorValues values;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
        values.ActorValuesList[cIndex] = values.ActorMaxValuesList[cIndex] = 100.f;
    return values;
}

// The engine's temporary forms (a reference placed at run time, a copy's NPC): 0xFF ids; a deleted one's id goes to
// the next (docs/re/crashes/2026-10-14-reused-copy-form-id.md).
uint32_t FakeEngine::NewTemporary() noexcept
{
    ++m_made;
    if (m_freedTemporary.empty())
        return m_nextTemporary++;
    return m_freedTemporary.extract(m_freedTemporary.begin()).value();
}

uint32_t FakeEngine::LoadNpc(uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                        .Essential = EssentialBases.contains(aBase), .Made = m_made};
    return cId;
}

void FakeEngine::PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    Actors[aFormId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                            .Essential = EssentialBases.contains(aBase)};
}

void FakeEngine::FinishLoading(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.Initializing = false;
    actor.Loaded = true;
    World::Get().GetRunner().Trigger(ReferenceLoadedEvent{aFormId});
}

void FakeEngine::CheckHealth(uint32_t aFormId, Actor& aActor) noexcept
{
    if (aActor.Copy || aActor.Dead || aActor.Values.ActorValuesList[kHealth] > 0.f)
        return;
    (aActor.Essential ? aActor.Bleeding : aActor.Dead) = true;
    if (aActor.Dead)
    {
        aActor.GraphUpdating = false;
        aActor.GraphState = "dead";
        if (aFormId == kPlayer)
        {
            aActor.DyingFrames = kDyingFrames;
            aActor.DeadFrames = 0;
            // KillImpl's death drop (37320): the drawn weapon falls, unless our hook keeps a player's.
            if (aActor.Weapon == WeaponState::Drawn && !DeathDrops::Kept(aActor.Player, World::Get().GetTransport().IsOnline()))
                if (const auto cHeld = std::ranges::find_if(aActor.Items.Entries, [&](const auto& acEntry) {
                        const auto cForm = ItemForms.find(::ItemForm(acEntry.BaseId));
                        return acEntry.ExtraWorn && cForm != ItemForms.end() && cForm->second.Type == kWeapon;
                    });
                    cHeld != aActor.Items.Entries.end())
                {
                    Inventory::Entry dropped = *cHeld;
                    dropped.Count = 1;
                    dropped.ExtraWorn = false;
                    DropsInWorld(dropped);
                }
        }
        auto& action = aActor.LatestAnimation;
        action.Tick = World::Get().GetFrameTick();
        action.ActorId = aFormId;
        action.ActionId = 0x1337;
        action.State1 = DeathActions::kDying;
        action.EventName = "DeathAnim";
        World::Get().GetDispatcher().trigger(action);
        World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, false});
    }
}

void FakeEngine::Hit(uint32_t aFormId, float aDamage)
{
    DamageActorHealth(aFormId, 0, aDamage);
}

std::vector<uint32_t> FakeEngine::CopiesOf(uint32_t aBase) const
{
    std::vector<uint32_t> copies;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Copy && (!aBase || cActor.Base == aBase))
            copies.push_back(cId);
    return copies;
}

// The process lists: the actors in a cell loaded here (copies too: they are this game's actors), but the player.
std::vector<uint32_t> FakeEngine::GetLoadedActorFormIds() const noexcept
{
    std::vector<uint32_t> loaded;
    for (const auto& [cId, cActor] : Actors)
        if (cId != kPlayer && ActorHas3D(cId))
            loaded.push_back(cId);
    return loaded;
}

bool FakeEngine::ActorHas3D(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Loaded && !it->second.Disabled && (aFormId == kPlayer || Attached.contains(it->second.Cell));
}

uint32_t FakeEngine::GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept
{
    const auto it = Actors.find(aActorFormId);
    return it != Actors.end() && aSlot < 2 ? it->second.HeldSpells[aSlot] : 0;
}

void FakeEngine::EquipSpells(uint32_t aFormId, const MagicEquipment& acMagic)
{
    auto& actor = Actors.at(aFormId);
    actor.Items.CurrentMagicEquipment = acMagic;
    if (ActorHas3D(aFormId))
    {
        actor.HeldSpells[0] = ::ItemForm(acMagic.LeftHandSpell);
        actor.HeldSpells[1] = ::ItemForm(acMagic.RightHandSpell);
    }
}

void FakeEngine::RevertSelectedSpells(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.Items.CurrentMagicEquipment.LeftHandSpell = actor.Items.CurrentMagicEquipment.RightHandSpell = {};
    actor.HeldSpells[0] = actor.HeldSpells[1] = 0;
}

void FakeEngine::Unload3D(uint32_t aFormId)
{
    Actors.at(aFormId).Loaded = false;
    RevertSelectedSpells(aFormId);
}

void FakeEngine::PlayerTravels(uint32_t aCell, const glm::vec3& acPosition)
{
    MoveActorTo(kPlayer, aCell, acPosition.x, acPosition.y, acPosition.z);
}

void FakeEngine::SetActorCopy(uint32_t aFormId, bool aCopy) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cReleased = it->second.Copy && !aCopy;
        it->second.Copy = aCopy;
        if (!aCopy)
            it->second.OwnerCombatTarget = 0;
        // Skyrim: only Actor::EndInterruptPackage (37474) ends it; the engine then picks the actor's own package.
        if (cReleased && CopyPackage::EndsOnRelease(it->second.RunningPackageType, it->second.DoNothing.packFlags))
            it->second.RunningPackageType = 0;
    }
}

bool FakeEngine::IsActorCopy(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy;
}

void FakeEngine::SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Player = aPlayer;
}

uint32_t FakeEngine::GetActorBaseFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Base : 0;
}

bool FakeEngine::SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !aBaseFormId)
        return false;
    it->second.Base = aBaseFormId;
    return true;
}

void FakeEngine::SetActorDisplayName(uint32_t aFormId, const std::string& acName) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.DisplayName = PooledNames.try_emplace(NameKey(acName), acName).first->second;
}

std::string FakeEngine::GetFormName(uint32_t aFormId) const noexcept
{
    if (aFormId == 0x7)
        return PlayerCharacterName();
    const auto it = FormNames.find(aFormId);
    return it != FormNames.end() ? it->second : Name;
}

std::string FakeEngine::GetActorDisplayName(uint32_t aFormId) const noexcept
{
    const auto actor = Actors.find(aFormId);
    if (actor != Actors.end() && !actor->second.DisplayName.empty())
        return actor->second.DisplayName;
    if (aFormId == kPlayer)
        return PlayerCharacterName();
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Name : std::string{};
}

float FakeEngine::GetActorBaseWeight(uint32_t aFormId) const noexcept
{
    if (aFormId == kPlayer)
        return Player.Weight;
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Weight : 50.f;
}

uint32_t FakeEngine::GetActorRaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Race : 0;
}

uint32_t FakeEngine::GetActorParentCellFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0;
    // A persistent reference outdoors out of the loaded cells has none (Olava, Skulvar: Skyrim 2026-11-06).
    if (it->second.Persistent && it->second.WorldSpace && !Attached.contains(it->second.Cell))
        return 0;
    return it->second.Cell;
}

uint32_t FakeEngine::GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.WorldSpace : 0;
}

void FakeEngine::GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Position : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

void FakeEngine::GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Rotation : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

bool FakeEngine::IsActorDead(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Dead;
}

void FakeEngine::KillActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        (it->second.Essential ? it->second.Bleeding : it->second.Dead) = true;
        if (it->second.Dead)
        {
            it->second.GraphUpdating = false;
            it->second.GraphState = "dead";
        }
        if (!cWasDead && it->second.Dead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
    }
}

void FakeEngine::FinishDeath(uint32_t aFormId)
{
    Actors.at(aFormId).BlockResurrect = false;
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
}

void FakeEngine::ReplayDeathAction(uint32_t aFormId, const ActionEvent& acAction)
{
    Actors.at(aFormId).PoseHeldUntil = 0;
    if (!RefuseDeathActions)
        KillActor(aFormId);
    Actors.at(aFormId).LatestAnimation = acAction;
    World::Get().GetDispatcher().trigger(DeathActionReplayedEvent{aFormId, acAction.Tick});
}

bool FakeEngine::PlayReplayed(uint32_t aFormId, const ActionEvent& acAction)
{
    return ::ReplayState::HeldByPair(Actors.at(aFormId).InKillMove, !acAction.TargetEventName.empty()) || ReplayAnimation(aFormId, acAction);
}

void FakeEngine::StopPairClip(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    if (actor.StopsDropped)
    {
        --actor.StopsDropped;
        return;
    }
    actor.InPairClip = false;
    actor.PairBlowIn = 0;
}

void FakeEngine::ClipKillActor(uint32_t aFormId)
{
    if (auto& actor = Actors.at(aFormId); actor.Copy)
        actor.PoseHeldUntil = World::Get().GetFrameTick() + ::ReplayState::cPoseHoldMs;
}

bool FakeEngine::PoseHeld(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && ::ReplayState::PoseHeld(World::Get().GetFrameTick(), it->second.PoseHeldUntil, it->second.Dead);
}

void FakeEngine::RagdollContact(uint32_t aFormId)
{
    if (const auto& cActor = Actors.at(aFormId); cActor.InKillMove && ::ReplayState::ContactKills(cActor.PairedWithCopy))
        KillActor(aFormId);
}

void FakeEngine::EndKillMove(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.InKillMove = false;
    if (actor.KilledBy)
        KillActor(aFormId);
}

bool FakeEngine::ReplayAnimation(uint32_t aFormId, const ActionEvent& acAction)
{
    auto& actor = Actors.at(aFormId);
    if (const auto cPartner = Actors.find(acAction.TargetId); !acAction.TargetEventName.empty() && cPartner != Actors.end())
    {
        if (!(actor.InKillMove && cPartner->second.InKillMove) && !::ReplayState::PairMarkClear(!PairMarkBlocked, actor.Copy))
            return false;
        // An uninterruptible transition still playing drops the new pair's event too.
        if ((actor.InPairClip && actor.StopsDropped) || (cPartner->second.InPairClip && cPartner->second.StopsDropped))
            return false;
        // 32803 takes a pair on two actors already paired as the same pair; the second clip ends both at once.
        const bool cCollapsed = actor.InKillMove && cPartner->second.InKillMove;
        CollapsedPairs += cCollapsed;
        actor.InKillMove = cPartner->second.InKillMove = !cCollapsed;
        actor.LatestPairAction = acAction;
        if (PairBlows && !cCollapsed && !actor.Copy && acAction.EventName.starts_with("pa_"))
        {
            actor.PairBlowIn = PairBlowFrames;
            actor.PairVictim = acAction.TargetId;
        }
        actor.InPairClip = cPartner->second.InPairClip = !cCollapsed;
        actor.PairedWithCopy = cPartner->second.Copy;
        cPartner->second.PairedWithCopy = actor.Copy;
        actor.StopsDropped = cPartner->second.StopsDropped = PairStopsDropped;
    }
    else if (actor.Copy && actor.InKillMove)
        ++BrokenPairs;
    const auto cWeaponBefore = actor.Weapon;
    if (actor.Copy)
        actor.Weapon = WeaponState((ReplayState::Flags2(acAction.State2, uint32_t(actor.Weapon) << 5) >> 5) & 7);
    if (!GraphTransitions.empty() && !actor.GraphState.empty())
    {
        const auto it = GraphTransitions.find({actor.GraphState, acAction.EventName});
        if (it == GraphTransitions.end())
        {
            ++RefusedAnimations[aFormId];
            if (!ReplayGraph::Recover(false, acAction.GraphState, [&](std::string_view) {
                return LoadActorGraphState(aFormId, acAction.GraphState);
            }))
            {
                actor.Weapon = cWeaponBefore;
                return false;
            }
        }
        else
        {
            actor.GraphState = it->second;
            if (const auto cPicks = GraphPicks.find(actor.GraphState); cPicks != GraphPicks.end())
                actor.GraphState = cPicks->second[(DrawGraphRandom(actor) >> 13) % cPicks->second.size()];
            if (ClipFiles.contains(acAction.EventName) && !actor.LoadedClips.contains(acAction.EventName))
                if (const auto cNext = SkippedClips.find(actor.GraphState); cNext != SkippedClips.end())
                    actor.GraphState = cNext->second;
        }
    }
    return true;
}

uint32_t FakeEngine::DrawGraphRandom(Actor& aActor) noexcept
{
    return aActor.GraphRandom = aActor.GraphRandom * 1664525 + 1013904223;
}

std::shared_ptr<std::atomic<bool>> FakeEngine::RequestActionClips(uint32_t aFormId, const std::string& acEvent)
{
    if (!ClipFiles.contains(acEvent))
        return nullptr;
    auto done = std::make_shared<std::atomic<bool>>(false);
    ClipLoads.push_back({aFormId, acEvent, ClipLoadFrames, done});
    return done;
}

bool FakeEngine::PerformAnimation(uint32_t aFormId, ActionEvent aAction, bool aQuery)
{
    aAction.State2 = (aAction.State2 & ~ReplayState::kWeaponMask) | (uint32_t(Actors.at(aFormId).Weapon) << 5);
    uint32_t flags = aQuery ? 1 : 0;
    aAction.Type = 2 | (flags ? 4 : 0);
    aAction.Tick = World::Get().GetFrameTick();
    aAction.ActorId = aFormId;
    aAction.Variables.GraphRandom = Actors.at(aFormId).GraphRandom;
    if (ClipFiles.contains(aAction.EventName))
        Actors.at(aFormId).LoadedClips.insert(aAction.EventName);
    const auto cOutcome = ReplayCapture::Run(flags, [&]() -> uint8_t {
        // PerformAction (38949) resolves a query, changes flag 1 to 2 and never sends its event.
        if (flags & 1)
        {
            flags = 2;
            return 1;
        }
        return ReplayAnimation(aFormId, aAction);
    });
    if (cOutcome.Capture && cOutcome.Result)
    {
        if (::ReplayState::StartsAttack(aAction.EventName))
            Actors.at(aFormId).ClashUntil = 0;
        if (aAction.TargetEventName.empty())
            aAction.GraphState = SaveActorGraphState(aFormId);
        Actors.at(aFormId).LatestAnimation = aAction;
        World::Get().GetDispatcher().trigger(aAction);
    }
    return cOutcome.Result != 0;
}

ActionEvent FakeEngine::GetActorLatestAnimation(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.LatestAnimation : ActionEvent{};
}

namespace
{
// SaveState 63601's layout for one graph ("fake") with one active machine ("Root") in acState.
std::string FakeGraphBlock(const std::string& acState)
{
    std::string graph;
    const auto cPut = [&graph](const void* apData, size_t aSize) { graph.append(static_cast<const char*>(apData), aSize); };
    const auto cString = [&](std::string_view acText) {
        const auto cLength = uint16_t(acText.size());
        cPut(&cLength, 2);
        cPut(acText.data(), acText.size());
    };
    const uint32_t cOne = 1, cZero = 0;
    cPut(&cOne, 4);
    cString("Root");
    cPut(&cZero, 4);
    cString(acState);
    cPut(&cZero, 4);
    cPut(&cZero, 4);
    std::string block;
    const uint16_t cProject = 4;
    block.append(reinterpret_cast<const char*>(&cProject), 2).append("fake");
    const auto cSize = uint32_t(graph.size());
    block.append(reinterpret_cast<const char*>(&cSize), 4).append(graph);
    return block;
}

// FakeGraphBlock's state, after the save's 8 bytes of ActorState: none unless it is one graph with one machine.
std::optional<std::string> FakeGraphState(const std::string& acSaved)
{
    size_t at = 8;
    bool bad = false;
    const auto cTake = [&]<class T>(T) {
        T value{};
        bad = bad || at + sizeof(T) > acSaved.size();
        if (!bad)
            std::memcpy(&value, acSaved.data() + at, sizeof(T));
        at += sizeof(T);
        return value;
    };
    const auto cString = [&] {
        const auto cLength = cTake(uint16_t{});
        bad = bad || at + cLength > acSaved.size();
        std::string text = bad ? std::string{} : acSaved.substr(at, cLength);
        at += cLength;
        return text;
    };
    cString();
    cTake(uint32_t{});
    const auto cMachines = cTake(uint32_t{});
    cString();
    cTake(uint32_t{});
    auto state = cString();
    cTake(uint32_t{});
    cTake(uint32_t{});
    if (bad || cMachines != 1 || at != acSaved.size())
        return std::nullopt;
    return state;
}
} // namespace

std::string FakeEngine::SaveActorGraphState(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || it->second.GraphState.empty())
        return {};
    const uint32_t cFlags[2] = {0, uint32_t(it->second.Weapon) << 5};
    return std::string(reinterpret_cast<const char*>(cFlags), sizeof(cFlags)) + FakeGraphBlock(it->second.GraphState);
}

bool FakeEngine::LoadActorGraphState(uint32_t aFormId, const std::string& acState) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    auto& actor = it->second;
    actor.LoopFrames = 0; // clip times are not saved: a loaded graph's clips start over
    // TESObjectREFR::SaveAnimationGraphState prefixes its graph with both ActorState words.
    if (acState.size() >= 8 && acState.compare(0, 4, std::string(4, '\0')) == 0)
    {
        uint32_t flags[2];
        std::memcpy(flags, acState.data(), sizeof(flags));
        const uint32_t cFlags2 = actor.Copy ? ReplayState::Flags2(flags[1], uint32_t(actor.Weapon) << 5) : flags[1];
        actor.Weapon = WeaponState((cFlags2 >> 5) & 7);
        // One graph, one active machine: its state is the fake's GraphState.
        const auto cState = FakeGraphState(acState);
        if (!cState)
            return false;
        actor.GraphState = *cState;
    }
    else
        actor.GraphState = acState; // direct fixtures supply the graph alone
    return true;
}

void FakeEngine::SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.LatestAnimation = acAction;
}

// Actor::Respawn: alive again, values restored; the value writers and Resurrect report it.
void FakeEngine::RespawnActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        const auto cAction = it->second.LatestAnimation;
        it->second.Dead = it->second.Bleeding = false;
        it->second.LatestAnimation = {};
        it->second.GraphState.clear();
        it->second.GraphUpdating = true;
        it->second.Values = FullValues();
        it->second.Bases = FullValues().ActorMaxValuesList;
        for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        if (cWasDead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, true, true,
                cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
    }
}

void FakeEngine::ResurrectActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ResurrectCalls;
        if (it->second.BlockResurrect)
            return;
        bool forced = false;
        DeathActions::WithLocalReset(forced, [&] {
            if (it->second.Dead && !forced)
            {
                ActionEvent reset = it->second.LatestAnimation;
                reset.Tick = World::Get().GetFrameTick();
                reset.ActionId = 0xD1FDE;
                reset.EventName = "IdleForceDefaultState";
                World::Get().GetDispatcher().trigger(reset);
            }
            const auto cGraph = it->second.GraphState;
            const bool cUpdating = it->second.GraphUpdating;
            RespawnActor(aFormId);
            if (it->second.Copy)
            {
                it->second.GraphState = cGraph;
                it->second.GraphUpdating = cUpdating;
            }
        });
    }
}

bool FakeEngine::DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    const bool cWasDead = it->second.Dead;
    it->second.Values.ActorValuesList[kHealth] -= aDamage;
    if (aDamage > 0 && aAttackerFormId)
        World::Get().GetRunner().Trigger(ActorDamagedEvent{aFormId, aAttackerFormId, aDamage});
    World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, kHealth});
    // The engine names the killer before its death event.
    if (!cWasDead && it->second.Values.ActorValuesList[kHealth] <= 0.f)
        it->second.KilledBy = aAttackerFormId;
    CheckHealth(aFormId, it->second);
    return true;
}

bool FakeEngine::IsBleedingOut(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Bleeding;
}

bool FakeEngine::IsOtherPlayer(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy && it->second.Player;
}

bool FakeEngine::SetActorPersistent(uint32_t aFormId, bool aPersistent) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        return std::exchange(it->second.Persistent, aPersistent);
    return false;
}

// The player's faction is not modelled.
void FakeEngine::SetActorPlayerFaction(uint32_t) noexcept
{
}

// Stepped or placed, never a copy lying in its ragdoll (SkyrimBridge); graph variables not modelled. A step reaches the
// goal across, never up: Havok's step leaves height to support and gravity (actor-movement.md; flat ground here).
bool FakeEngine::ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3], const float aRotation[3], float,
                                     const AnimationVariables& acVariables, CopyMove aMove) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    it->second.GraphVariables = acVariables;
    if (aMove == CopyMove::Leave || RagdollPosed(aFormId))
        return true;
    it->second.Stepped = aMove == CopyMove::Step;
    ++it->second.Movements;
    const float cHeight = it->second.Position[2];
    std::copy_n(aPosition, 3, it->second.Position);
    if (it->second.Stepped)
        it->second.Position[2] = std::min(cHeight, aPosition[2]);
    // Its capsule meets its furniture; the write-back after physics moves it where it was pushed (actor-movement.md).
    // Actor::Move clears the no-collision bit out of furniture (37507, 0x14068990A).
    if (it->second.Stepped && !it->second.InFurniture)
        it->second.Collides = true;
    if (it->second.Stepped && it->second.InFurniture && it->second.Collides)
        it->second.Position[1] -= kFurnitureOffset * static_cast<float>(1 + it->second.Movements % 3);
    std::copy_n(aRotation, 3, it->second.Rotation);
    // Outdoors its parent cell follows where it stands.
    if (it->second.WorldSpace)
        if (const uint32_t cCell = CellAt(it->second.WorldSpace, {aPosition[0], aPosition[1], aPosition[2]}))
        {
            const uint32_t cFrom = std::exchange(it->second.Cell, cCell);
            CellChanged(aFormId, cFrom);
        }
    return true;
}

float FakeEngine::GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++ValueReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++MaximumReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorMaxValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorTemporaryValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    const auto actor = Actors.find(aFormId);
    if (actor == Actors.end())
        return 0.f;
    const auto value = actor->second.TemporaryValues.find(static_cast<uint32_t>(aIndex));
    return value == actor->second.TemporaryValues.end() ? 0.f : value->second;
}

// Its base (SetActorValue's), 0 until set.
float FakeEngine::GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto cBase = it->second.Bases.find(static_cast<uint32_t>(aIndex));
    return cBase != it->second.Bases.end() ? cBase->second : 0.f;
}

void FakeEngine::SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ValueWrites;
        const auto cIndex = static_cast<uint32_t>(aIndex);
        auto& base = it->second.Bases[cIndex];
        const float cDelta = aValue - base;
        if (cDelta != 0.f)
        {
            base = aValue;
            it->second.Values.ActorValuesList[cIndex] += cDelta;
            it->second.Values.ActorMaxValuesList[cIndex] += cDelta;
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        }
    }
}

// Permanent and temporary modifiers move the current; damage cannot heal above their combined maximum.
void FakeEngine::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    auto& values = it->second.Values;
    ++it->second.ValueWrites;
    const auto cBefore = values;
    const auto cIndex = static_cast<uint32_t>(aIndex);
    if (aForceMode == 0)
    {
        values.ActorValuesList[cIndex] += aValue - values.ActorMaxValuesList[cIndex];
        values.ActorMaxValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 1)
    {
        it->second.TemporaryValues[cIndex] += aValue - values.ActorValuesList[cIndex];
        values.ActorValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 2)
        values.ActorValuesList[cIndex] = std::min(aValue, values.ActorMaxValuesList[cIndex] + it->second.TemporaryValues[cIndex]);
    if (cBefore != values)
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
    if (cIndex == kHealth)
        CheckHealth(aFormId, it->second);
}

bool FakeEngine::IsActorStillLoading(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Initializing;
}

bool FakeEngine::IsActorDisabled(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Disabled;
}

bool FakeEngine::IsActorDeleted(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Deleted;
}

// Every enable is TESObjectREFR::Enable (19800), which the plugin refuses for a hidden copy.
void FakeEngine::EnableActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end() && !it->second.Hidden)
        it->second.Disabled = false;
}

void FakeEngine::SetActorHidden(uint32_t aFormId, bool aHidden) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Hidden = aHidden;
        it->second.Disabled = aHidden;
    }
}

void FakeEngine::DisableActor(uint32_t aFormId, bool aDelete) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Disabled = true;
        it->second.Deleted |= aDelete;
    }
}

void FakeEngine::MarkForDelete(uint32_t aFormId)
{
    Actor& actor = Actors.at(aFormId);
    actor.Deleted = true;
    actor.NoProcess = true;
    actor.Loaded = false;
}

// A copy: a new temporary actor of that NPC, where this game's player is (as PlaceAtMe from the player).
uint32_t FakeEngine::SpawnActorFromNpc(uint32_t aNpcFormId) noexcept
{
    ++Spawned[aNpcFormId];
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aNpcFormId, .Cell = Actors.at(kPlayer).Cell, .WorldSpace = Actors.at(kPlayer).WorldSpace,
                        .Created = true, .Essential = EssentialBases.contains(aNpcFormId), .Made = m_made};
    return cId;
}

namespace
{
// The race a player's appearance data holds (PlayerSwitchesRace writes "race <id>"), 0 for none.
uint32_t RaceInLooks(const std::string& acAppearance)
{
    uint32_t race = 0;
    return std::sscanf(acAppearance.c_str(), "race %X", &race) == 1 ? race : 0;
}
} // namespace

// A player's copy: a temporary NPC made from their appearance, and an actor of it.
// A base built from a snapshot: the name only if its section (0x20) came, as FormState::Load.
void FakeEngine::LoadBase(uint32_t aBase, const std::string& acAppearance, uint32_t aChangeFlags)
{
    auto& base = Npcs[aBase];
    const size_t cNameEnd = aChangeFlags & 0x20 ? acAppearance.find('\0') : std::string::npos;
    if (cNameEnd != std::string::npos)
        base.Name = acAppearance.substr(0, cNameEnd);
    base.Looks = cNameEnd == std::string::npos ? acAppearance : acAppearance.substr(cNameEnd + 1);
}

uint32_t FakeEngine::CreateCustomActor(const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept
{
    const uint32_t cBase = NewTemporary();
    Npcs[cBase].Weight = aWeight;
    LoadBase(cBase, acAppearance, aChangeFlags);
    Npcs[cBase].Race = RaceInLooks(Npcs[cBase].Looks);
    return SpawnActorFromNpc(cBase);
}

uint32_t FakeEngine::GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it == Npcs.end() ? 0 : it->second.FaceBase;
}

uint32_t FakeEngine::SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept
{
    if (aFormId == kPlayer)
    {
        // As native FormState: the name only in its section (0x20), which only naming marks.
        aBuffer = Player.BaseFlags & 0x20 ? PlayerCharacterName() + '\0' + Player.Base : Player.Base;
        return Player.BaseFlags;
    }
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    if (it == Npcs.end())
        return 0;
    const uint32_t cFlags = NpcSnapshot::SaveFlags(it->second.ChangeFlags, false,
                                                 GetActorBaseFormId(aFormId) >= 0xFF000000, it->second.FaceBase != 0);
    // Native FormState saves the full name only when its section (0x20) was requested.
    if (cFlags & 0x20)
        aBuffer = it->second.Name + '\0' + it->second.Looks;
    else if (cFlags)
        aBuffer = "native non-name sections";
    return cFlags;
}

// TESForm::MarkChanged: the sections are saved from now on.
void FakeEngine::MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept
{
    if (aFormId == kPlayer)
        Player.BaseFlags |= aFlags;
    else if (const auto it = Npcs.find(GetActorBaseFormId(aFormId));
             it != Npcs.end() && GetActorBaseFormId(aFormId) < 0xFF000000)
        it->second.ChangeFlags |= aFlags;
}

// The player's is a travel (MoveTo_Impl queues it as a door's): the cells load and unload, a teleport is reported.
void FakeEngine::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    // MoveTo tears down the native mount interaction, including when the moved reference is the horse.
    std::erase_if(Mounts, [&](const auto& acMount) { return acMount.first == aFormId || acMount.second == aFormId; });
    const uint32_t cFrom = it->second.Cell;
    const bool cHad3D = ActorHas3D(aFormId);
    const auto cExterior = Exteriors.find(aCellId);
    it->second.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    // Outdoors the cell at the position, as MoveTo picks it.
    const uint32_t cAt = it->second.WorldSpace ? CellAt(it->second.WorldSpace, {aX, aY, aZ}) : 0;
    it->second.Cell = cAt ? cAt : aCellId;
    it->second.Position[0] = aX;
    it->second.Position[1] = aY;
    it->second.Position[2] = aZ;
    // An NPC moved to another cell drops its 3D, and with it its hand spells (seen on a player's copy, 2026-11-04).
    if (aFormId != kPlayer && it->second.Cell != cFrom && cHad3D)
        RevertSelectedSpells(aFormId);
    CellChanged(aFormId, cFrom);
    // A created actor landing in a cell not loaded goes as with its cell's unload (measured: a player's copy spawned
    // four cells off is gone that frame, before SetActorPersistent).
    if (aFormId != kPlayer && it->second.Created && !it->second.Persistent && !Attached.contains(it->second.Cell))
    {
        DeleteActor(aFormId);
        return;
    }
    // Enabled, UpdateProcessLevel (37501) frees it; disabled, its cell's unload does (TESObjectCELL 19113).
    if (it->second.Collectable && !it->second.Persistent && !Attached.contains(it->second.Cell))
    {
        Actors.erase(it);
        return;
    }
    if (aFormId == kPlayer)
    {
        // Party travel announces it (PlayerCharacter.cpp's 40438), but another game's party call's move.
        if (World::Get().GetTransport().IsOnline() && !m_mirroredMove)
            World::Get().GetRunner().Trigger(PlayerTravelQueuedEvent{
                it->second.WorldSpace, it->second.WorldSpace ? 0u : it->second.Cell, {aX, aY, aZ}, Heading});
        World::Get().ctx().at<FakeDialogue&>().End();
        LoadAroundPlayer(cFrom);
        Teleported = true;
        ++Travels;
    }
}

void FakeEngine::CellChanged(uint32_t aFormId, uint32_t aFrom)
{
    const Actor& cActor = Actors.at(aFormId);
    const auto cLocation = [&](uint32_t aCell) {
        const auto it = Locations.find(aCell);
        return it != Locations.end() ? it->second : 0u;
    };
    if (aFormId == kPlayer && cLocation(aFrom) != cLocation(cActor.Cell))
        RaisesStoryEvent(StoryEvent::TypeOf("CLOC"), {kPlayer, cLocation(aFrom), cLocation(cActor.Cell)});
}

void FakeEngine::RaisesStoryEvent(uint32_t aType, std::vector<uint64_t> aMembers)
{
    const auto it = std::ranges::find(StoryTypes, aType, &RegisteredStory::Id);
    if (DecidesWorld || it == StoryTypes.end())
        return;
    OwnStoryEvent event{static_cast<uint32_t>(it - StoryTypes.begin())};
    std::ranges::copy(aMembers | std::views::take(event.Members.size()), event.Members.begin());
    World::Get().GetRunner().Trigger(event);
}

void FakeEngine::Exterior(uint32_t aWorldSpace, uint32_t aFirstCell, int32_t aRadius)
{
    uint32_t cell = aFirstCell;
    for (int32_t x = -aRadius; x <= aRadius; ++x)
        for (int32_t y = -aRadius; y <= aRadius; ++y)
            Exteriors[cell++] = {aWorldSpace, x, y};
}

uint32_t FakeEngine::LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept
{
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpaceFormId && cCell.X == aX && cCell.Y == aY)
        {
            if (!AtMainLoop) WorkerCellLoads.push_back(cId);
            return cId;
        }
    return 0;
}

uint32_t FakeEngine::CellAt(uint32_t aWorldSpace, const glm::vec3& acPosition) const noexcept
{
    const auto cAt = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    // Native movement resolves an already known cell; the bridge lookup can load one from disk.
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpace && cCell.X == cAt.X && cCell.Y == cAt.Y)
            return cId;
    return 0;
}

// The loaded grid's centre is the player's cell (TES::centerGridX/Y; the engine's own lag behind it not modelled).
void FakeEngine::LoadAroundPlayer(uint32_t aFrom)
{
    const auto& cPlayer = Actors.at(kPlayer);
    std::set<uint32_t> cells{cPlayer.Cell};
    if (const auto cCentre = Exteriors.find(cPlayer.Cell); cCentre != Exteriors.end())
        for (const auto& [cId, cCell] : Exteriors)
            if (cCell.WorldSpace == cCentre->second.WorldSpace &&
                GridCellCoords::IsCellInGridCell({cCell.X, cCell.Y}, {cCentre->second.X, cCentre->second.Y}, false))
                cells.insert(cId);
    std::set<uint32_t> left = std::exchange(m_playerCells, cells);
    left.insert(aFrom);
    for (const uint32_t cCell : left)
        if (!cells.contains(cCell))
            UnloadCell(cCell);
    for (const uint32_t cCell : cells)
        LoadCell(cCell);
}

void FakeEngine::PlayerStands(uint32_t aCell, const glm::vec3& acPosition)
{
    auto& player = Actors.at(kPlayer);
    const uint32_t cFrom = player.Cell;
    player.Cell = aCell;
    const auto cExterior = Exteriors.find(aCell);
    player.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    std::copy_n(&acPosition.x, 3, player.Position);
    LoadAroundPlayer(cFrom);
}

void FakeEngine::PlayerWalks(const glm::vec3& acPosition)
{
    const auto& cPlayer = Actors.at(kPlayer);
    PlayerStands(CellAt(cPlayer.WorldSpace, acPosition), acPosition);
}

void FakeEngine::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Rotation[0] = aX;
        it->second.Rotation[2] = aZ;
    }
}

// As SkyrimBridge's: only a reference made at run time is deleted, never the plugins' or the player.
void FakeEngine::DeleteActor(uint32_t aFormId) noexcept
{
    if (!IsActorTemporary(aFormId) || !Actors.erase(aFormId))
        return;
    m_freedTemporary.insert(aFormId);
}

bool FakeEngine::IsActorNpcTemporary(uint32_t aFormId) const noexcept
{
    return GetActorBaseFormId(aFormId) >= 0xFF000000;
}

Inventory FakeEngine::GetActorInventory(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Items : Inventory{};
}

void FakeEngine::SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    // As AddOrRemoveItem: an item with no form here is not added, a coating with no poison here is lost.
    it->second.Items = acInventory;
    std::erase_if(it->second.Items.Entries, [](const Inventory::Entry& acEntry) { return !::ItemForm(acEntry.BaseId); });
    for (auto& entry : it->second.Items.Entries)
        if (entry.ExtraPoisonId && !::ItemForm(entry.ExtraPoisonId))
            entry.ExtraPoisonId = {}, entry.ExtraPoisonCount = 0;
    EquipSpells(aFormId, acInventory.CurrentMagicEquipment); // Actor::SetMagicEquipment
}

// Each value listed; the others stay. As Actor::SetActorValues: a pool's maximum and current; any other value's base
// made the current one, its maximum with it.
void FakeEngine::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    const auto cPool = [](uint32_t aIndex) { return aIndex >= kHealth && aIndex <= kStamina; };
    for (const auto& [cIndex, cValue] : acValues.ActorMaxValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 0, cIndex, cValue);
    for (const auto& [cIndex, cValue] : acValues.ActorTemporaryValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 1, cIndex, GetActorValue(aFormId, cIndex) + cValue - GetActorTemporaryValue(aFormId, cIndex));
    for (const auto& [cIndex, cValue] : acValues.ActorValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 2, cIndex, cValue);
        else
            SetActorValue(aFormId, cIndex, cValue);
}

// Resurrect: alive, its pools full (its maximums kept), its death action over, reported as the Resurrect hook reports
// it; placement is the party follower move.
void FakeEngine::RevivePlayer() noexcept
{
    auto& player = Actors.at(kPlayer);
    if (!player.Dead)
        return;
    player.Dead = false;
    player.DyingFrames = player.DeadFrames = 0;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
    {
        player.Values.ActorValuesList[cIndex] = player.Values.ActorMaxValuesList[cIndex];
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{kPlayer, cIndex});
    }
    const auto cAction = std::exchange(player.LatestAnimation, {});
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{kPlayer, true, true,
        cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
}

void FakeEngine::HoldDeathControls(bool aHold) noexcept
{
    constexpr uint32_t flags = 0x1 | 0x4 | 0x20 | 0x40 | 0x80 | 0x400;
    if (aHold && !DeathControlsHeld)
    {
        DeathControlsBefore = Controls & flags;
        Holds.Take(0xFFFFFFFE, flags);
        Controls &= ~flags;
    }
    else if (!aHold && DeathControlsHeld)
        Controls |= Holds.GiveBack(0xFFFFFFFE, flags) & DeathControlsBefore;
    DeathControlsHeld = aHold;
}

// PartyNatives' entry points (Papyrus/PartyNatives.h) on the current game's fake engine.
namespace PartyNatives
{
void SetInSession(bool aInSession) noexcept
{
    FakeEngine::Current().InSession = aInSession;
}

bool InSession() noexcept
{
    return FakeEngine::Current().InSession;
}

bool Run(const LocalCall& acCall, uint32_t aSource, uint32_t aKey) noexcept
{
    auto& engine = FakeEngine::Current();
    if (aKey)
        engine.Copies.Sent(aSource, aKey);
    engine.Queue(acCall, MirroredTag(aSource));
    return true;
}

void EndCopy(uint32_t aSource, uint32_t aKey, const LocalValue& acResult) noexcept
{
    auto& engine = FakeEngine::Current();
    if (const auto cStack = engine.Copies.Ended(aSource, aKey, acResult))
        engine.AnswerBox(*cStack, std::get<int32_t>(acResult));
}

void ForgetCopies(std::optional<uint32_t> aSource) noexcept
{
    auto& engine = FakeEngine::Current();
    if (aSource)
        engine.Copies.Left(*aSource);
    else
        engine.Copies = {};
}

void GiveBackControls(std::optional<uint32_t> aSource) noexcept
{
    auto& engine = FakeEngine::Current();
    for (const auto& cReturned : engine.Holds.Leave(aSource ? std::optional(MirroredTag(*aSource)) : std::nullopt))
    {
        if (!cReturned.Free)
            continue;
        const auto c = EnableFor(cReturned.Free, cReturned.NoneElse);
        engine.Queue(
            EnablePlayerControls(c.Movement, c.Fighting, c.CamSwitch, c.Looking, c.Sneaking, c.Menu, c.Activate),
            cReturned.Holder);
    }
}
} // namespace PartyNatives

// SystemQuests' seam: this game's list and instances (FakeEngine's).
namespace SystemQuests
{
bool Learn(uint32_t aQuest, bool aTell) noexcept
{
    auto& engine = FakeEngine::Current();
    if (!aQuest || !engine.SystemQuestList.Learn(aQuest))
        return false;
    if (const auto it = engine.QuestGlobals.find(aQuest); it != engine.QuestGlobals.end())
        engine.SystemQuestList.SetGlobals(aQuest, it->second);
    if (aTell)
        World::Get().GetRunner().Trigger(SystemQuestLearnedEvent{aQuest});
    return true;
}

bool Any() noexcept
{
    return FakeEngine::Current().SystemQuestList.Any();
}

bool Is(uint32_t aQuest) noexcept
{
    return FakeEngine::Current().SystemQuestList.Contains(aQuest);
}

bool IsGlobal(uint32_t aGlobal) noexcept
{
    return FakeEngine::Current().SystemQuestList.IsGlobal(aGlobal);
}

std::vector<uint32_t> All() noexcept
{
    return FakeEngine::Current().SystemQuestList.All();
}

void Clear() noexcept
{
    FakeEngine::Current().SystemQuestList.Clear();
}

void TimerCalled(uint32_t aQuest, std::string_view aNative, float aInterval) noexcept
{
    auto& engine = FakeEngine::Current();
    if (engine.SystemQuestList.Contains(aQuest))
        engine.QuestTimers[aQuest][std::string(aNative)] = {std::string(aNative), aInterval};
}

std::string ReadState() noexcept
{
    return FakeEngine::Current().ReadSystemQuests();
}

void ApplyState(std::string_view aState) noexcept
{
    FakeEngine::Current().ApplySystemQuests(aState);
}
} // namespace SystemQuests

void FakeEngine::PlacesMarker(const glm::vec3& acPosition, uint32_t aWorldSpace)
{
    Vector3_NetQuantize position;
    position.x = acPosition.x;
    position.y = acPosition.y;
    position.z = acPosition.z;
    World::Get().GetRunner().Trigger(SetWaypointEvent(position, aWorldSpace));
    Waypoint = Marker{acPosition, aWorldSpace};
}

void FakeEngine::ChoosesDifficulty(uint32_t aDifficulty)
{
    Difficulty = aDifficulty;
    World::Get().GetRunner().Trigger(DifficultyChangeEvent{aDifficulty});
}

void FakeEngine::PlayerSwitchesRace(uint32_t aRace)
{
    Npcs[Actors.at(kPlayer).Base].Race = aRace;
    Player.Base = std::format("race {:X}", aRace);
    Player.BaseFlags |= 0x2000000; // CHANGE_NPC_RACE
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerFormChangeEvent{});
}

void FakeEngine::PlayerUsesRaceMenu(const std::function<void()>& acChanges)
{
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", true});
    acChanges();
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", false});
}

void FakeEngine::PlayerNames(const std::string& acName)
{
    Player.Name = acName;
    Player.BaseFlags |= 0x20;
}

void FakeEngine::AliasNames(uint32_t aFormId, const std::string& acName)
{
    Actors.at(aFormId).DisplayName = acName;
    World::Get().GetRunner().Trigger(ActorRenamedEvent{aFormId});
}

std::string FakeEngine::PlayerId() noexcept
{
    return std::format("id:{:016x}", std::hash<std::string>{}(Name));
}

void FakeEngine::RunMainLoop() noexcept
{
    RunNativeCombat();
    if (const auto cName = std::exchange(PendingSave, std::nullopt))
    {
        if (RefusesSaves || !World::Get().ctx().at<SaveService>().OnSaveRequested())
            MadeSave = std::optional<SaveFiles>{};
        else
        {
            Saves.push_back(*cName);
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
            for (const auto& [id, r] : References)
            {
                Serialization::WriteVarInt(writer, id);
                Serialization::WriteBool(writer, r.Locked);
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
            MadeSave = SaveFiles{{reinterpret_cast<const char*>(buffer.GetData()), writer.Size()}, World::Get().ctx().at<SaveService>().WriteCoSave()};
            SavedFiles[*cName] = **MadeSave;
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
        References[id].Locked = Serialization::ReadBool(reader);
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
    Knowledge = saved.Knowledge;
    Player.Items = {};
    Player.Cell = {};
    Player.Position = {};
    Actor& player = Actors.at(kPlayer);
    player.Bleeding = player.Dead = false;
    player.DyingFrames = player.DeadFrames = 0;
    player.Values = FullValues();
    player.Items = saved.Items;
    player.Cell = saved.Cell.BaseId;
    std::copy(saved.Position.begin(), saved.Position.end(), player.Position);
    LoadCell(player.Cell);
    auto& saves = World::Get().ctx().at<SaveService>();
    saves.RevertCoSave();
    saves.ReadCoSave(cFiles.CoSave, CharacterRecords::kVersion);
    AtMainMenu = false;
    Loaded.push_back(cName);
    World::Get().GetRunner().Trigger(GameLoadedEvent{});
}

bool FakeEngine::Autosaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested(true))
        return false;
    Saves.push_back(acName);
    return true;
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
}

void FakeEngine::SpellHits(uint32_t aTarget, uint32_t aCaster, uint32_t aSpell, uint32_t aEffect, float aMagnitude)
{
    const auto cTarget = Actors.find(aTarget);
    if (cTarget == Actors.end())
        return;
    const bool cOurCast = Actors.contains(aCaster) && !Actors.at(aCaster).Copy;
    if (cTarget->second.Copy)
    {
        if (!cOurCast)
            return;
        AddTargetEvent event;
        event.TargetID = aTarget;
        event.CasterID = aCaster;
        event.SpellID = aSpell;
        event.EffectID = aEffect;
        event.Magnitude = aMagnitude;
        World::Get().GetRunner().Trigger(event);
        return;
    }
    Effects.push_back({aTarget, aCaster, aSpell, aEffect});
}

void FakeEngine::Fights(uint32_t aActor, uint32_t aTarget)
{
    auto& ai = Actors.at(aActor).Ai;
    ai.CombatState = aTarget ? 1 : 0;
    ai.CombatTargetId = aTarget;
    World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::AttackedBy(uint32_t aVictim, uint32_t aAttacker)
{
    // HookStartCombat's check (38561); its wiring is checked in Skyrim (scenario host-dies-guest-still-fought).
    auto& victim = Actors.at(aVictim);
    if (CopyCombat::MayStart(victim.Copy))
        victim.OwnCombatTarget = aAttacker;
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

std::vector<IActorBridge::ActiveEffectInfo> FakeEngine::GetActorActiveEffects(uint32_t aFormId) const noexcept
{
    std::vector<ActiveEffectInfo> effects;
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        for (const uint32_t cId : it->second.Visuals)
            effects.push_back({.EffectFormId = cId});
    return effects;
}

HitRules::Party FakeEngine::PartyOf(uint32_t aFormId) const noexcept
{
    using enum HitRules::Party;
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return None;
    if (aFormId == kPlayer)
        return LocalPlayer;
    if (it->second.Copy)
        return it->second.Player ? OtherPlayer : Copy;
    return Local;
}

void FakeEngine::TakesDamage(uint32_t aHittee, uint32_t aHitter, float aDamage, bool aApplyingEffect)
{
    switch (HitRules::Of(PartyOf(aHittee), PartyOf(aHitter), aApplyingEffect))
    {
    case HitRules::Route::Apply:
        World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage));
        DamageActorHealth(aHittee, aHitter, aDamage);
        break;
    case HitRules::Route::AskOwner: World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage, aHitter)); break;
    case HitRules::Route::Mirror:
    case HitRules::Route::Drop: break;
    }
}

void FakeEngine::Strikes(uint32_t aHitter, uint32_t aHittee, float aDamage)
{
    // HookDamageActor's check.
    if (const auto& cHitter = Actors.at(aHitter);
        ::ReplayState::CancelledBlow(World::Get().GetTick() < cHitter.ClashUntil))
        return;
    if (!Actors.contains(aHittee))
        return;
    World::Get().GetRunner().Trigger(HitEvent(aHitter, aHittee));
    TakesDamage(aHittee, aHitter, aDamage, false);
}

void FakeEngine::Shoots(uint32_t aShooter, uint32_t aSpell, const glm::vec3& acOrigin)
{
    ProjectileLaunchedEvent event{};
    event.Origin = acOrigin;
    event.ShooterID = aShooter;
    event.SpellID = aSpell;
    event.ParentCellID = Actors.at(aShooter).Cell;
    World::Get().GetRunner().Trigger(event);
    Shots.push_back({aShooter, aSpell, acOrigin});
}

void FakeEngine::Activates(uint32_t aActor, uint32_t aObject)
{
    ActivateEvent event;
    event.ObjectFormId = aObject;
    event.ActivatorFormId = aActor;
    event.ObjectParentCellFormId = Actors.contains(aObject) ? Actors.at(aObject).Cell : References.at(aObject).Cell;
    World::Get().GetRunner().Trigger(event);
    Activations.emplace_back(aObject, aActor);
    // The player activating a corpse opens its loot (TESNPC::Activate -> ContainerMenu::OpenMenu, our hook first).
    if (aActor == kPlayer && Actors.contains(aObject) && IsActorDead(aObject) && !LootMenuOf &&
        !ContainerOpens::Refused(Actors.at(aObject).Copy && Actors.at(aObject).Player))
    {
        LootMenuOf = aObject;
        World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    }
    ToggleDoor(aObject);
}

// The alias clear hooks report their quest after the engine removes the fill, without a stage/flag event.
void FakeEngine::ClearReferenceAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Aliases)
        if (fill.AliasId == aAlias)
        {
            fill = QuestAliasFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

void FakeEngine::ClearLocationAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Locations)
        if (fill.AliasId == aAlias)
        {
            fill = QuestLocationFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

// StatsMenu::ModifyPerkPoints writes directly; its hook marks both default globals after either signed delta.
void FakeEngine::ModifyBeastPerks(bool aVampire, int32_t aDelta)
{
    Globals.at(BeastPerkGlobals[aVampire ? 1 : 0]) += static_cast<float>(aDelta);
    GlobalWrites.insert(BeastPerkGlobals.begin(), BeastPerkGlobals.end());
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::PreviousSave() noexcept
{
    for (auto i = Saves.rbegin(); i != Saves.rend(); ++i)
        if (!i->starts_with("SkyrimCoop"))
            if (const auto f = SavedFiles.find(*i); f != SavedFiles.end()) return f->second;
    return std::nullopt;
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::ReadSave(const std::string& acName) noexcept
{
    const auto it = SavedFiles.find(acName);
    return it != SavedFiles.end() ? std::optional(it->second) : std::nullopt;
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

bool FakeEngine::ApplyActorLooks(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept
{
    ++AppearanceAttempts;
    if (!AtMainLoop || BlockAppearance || !Actors.contains(aFormId) || !Actors.at(aFormId).Copy)
        return false;
    auto& base = Npcs.at(GetActorBaseFormId(aFormId));
    LoadBase(GetActorBaseFormId(aFormId), acAppearance, aChangeFlags);
    const uint32_t cRace = RaceInLooks(base.Looks);
    if (base.Race != cRace)
    {
        auto& actor = Actors.at(aFormId);
        actor.Values = FullValues(); // SwitchRace recalculates values without a discrete value-write event.
        actor.Bases = actor.Values.ActorMaxValuesList;
        actor.TemporaryValues.clear();
    }
    base.Race = cRace;
    base.Weight = aWeight;
    return true;
}

bool FakeEngine::OpenFollowerInventory(uint32_t aNpcFormId) noexcept
{
    if (FollowerTradeNpc || !Actors.contains(aNpcFormId))
        return false;
    FollowerTradeNpc = aNpcFormId;
    World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    return true;
}

void FakeEngine::EndScene(uint32_t aActor)
{
    // TESSceneEvent can be delivered before the final actor scene field is cleared.
    World::Get().GetDispatcher().trigger(AiStateChangeEvent{aActor});
    Actors.at(aActor).Ai.SceneId = 0;
}
