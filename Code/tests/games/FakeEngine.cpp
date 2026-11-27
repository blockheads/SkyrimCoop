#include <CriticalStageRules.h>
#include <animation/DeferredClips.h>
#include <DisplayNames.h>
#include <FreedWornList.h>
#include <Diagnostics/HeadTrackRead.h>
#include <CopyCombat.h>
#include <core/MenuEvent.h>
#include <magic/InterruptCastEvent.h>
#include <characters/ActorHeadUpdateEvent.h>
#include <CheckedPlacement.h>
#include <core/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include <movement/CopyStep.h>
#include <magic/OwnerEffects.h>

#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <mounts/MountEvent.h>
#include <join/SaveWrittenEvent.h>

#include <animation/GraphState.h>
#include <animation/GraphStream.h>
#include <bit>
#include <set>
#include <span>
#include <characters/ActorRenamedEvent.h>
#include "FakeDialogue.h"
#include <core/PlayerTravelQueuedEvent.h>
#include <scripts/StoryEvents.h>

#include <items/ContainerChangeEvent.h>
#include <core/DifficultyChangeEvent.h>
#include <world/ActivateEvent.h>
#include <items/ContainerOpens.h>
#include <death/DeathDrops.h>
#include <combat/AddTargetEvent.h>
#include <magic/SummonEvent.h>
#include <actorstate/AiStateChangeEvent.h>
#include <magic/EffectVisualsChangeEvent.h>
#include <items/EquipmentChangeEvent.h>
#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDamagedEvent.h>
#include <characters/ActorValueChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <characters/NpcHeldEvent.h>
#include <GuestNpcs.h>
#include <actorstate/DeathActions.h>
#include <animation/ReplayCapture.h>
#include <animation/ReplayGraph.h>
#include <characters/NpcSnapshot.h>
#include <combat/HitEvent.h>
#include <animation/ActionRules.h>
#include <magic/EffectRules.h>
#include <magic/ProjectileLaunchedEvent.h>
#include <combat/TrapEvents.h>
#include <combat/TrapRules.h>
#include <magic/SpellCastEvent.h>
#include <items/InventoryChangeEvent.h>
#include <world/LockChangeEvent.h>
#include <items/LooseItemEvents.h>
#include <scripts/CallEndedEvent.h>
#include <scripts/RoutedCallEvent.h>
#include <core/PlayerProgressEvent.h>
#include <core/EngineNames.h>
#include <animation/ActorStateRules.h>
#include <animation/ReplayState.h>
#include <travel/RemoveWaypointEvent.h>
#include <travel/SetWaypointEvent.h>
#include <time/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <scripts/PartyNatives.h>
#include <scripts/SystemQuests.h>
#include <scripts/SystemQuestLearnedEvent.h>
#include <core/ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <core/World.h>

using namespace PartyRules;

void FakeEngine::Frame()
{
    ++m_frameCount;
    // The UI's calls (82082, 82083): a noted open item menu rebuilds its whole list (ItemMenuWatch::UiCall).
    if (ItemMenuNoted.Take(ItemMenuKind(), ItemMenuOf().Open))
        ItemMenuFreedLists = 0;
    // The task queue's kills (36993): counted under a hold (37896 -> 38617).
    for (auto& [cId, actor] : Actors)
        if (std::exchange(actor.KillQueued, false) && (actor.Copy || actor.KillHeld))
            actor.KillCounted = true;
    StepDeathRagdolls();
    StepCopiesToGoals();
    for (const auto [cRider, cMount] : std::map(std::exchange(MountRetryOf, {})))
        ActivateMount(cRider, cMount);
    for (auto it = DismountIn.begin(); it != DismountIn.end();)
        if (--it->second == 0)
        {
            const uint32_t cRider = it->first;
            it = DismountIn.erase(it);
            EndRide(cRider);
        }
        else
            ++it;
    // A draw 37279 took ends at its own weaponDraw (42824) or weaponSheathe (37169); a copy on its stream drops it.
    for (auto& [cId, actor] : Actors)
        if (actor.WeaponEventIn && --actor.WeaponEventIn == 0 && !(actor.Copy && World::Get().GetGraphStreams().OnStream(cId)))
            if (IsAnimating(actor.Weapon))
                actor.Weapon = actor.Weapon == WeaponState::Drawing ? WeaponState::Drawn : WeaponState::Sheathed;
    // A pair's clip runs on its own: its blow lands at HitFrame, a transition can hold off pairedStop until then.
    for (auto& [cId, actor] : Actors)
        if (actor.PairBlowIn && --actor.PairBlowIn == 0 && Actors.contains(actor.PairVictim))
        {
            Strikes(cId, actor.PairVictim, 1000.f);
            actor.KillMoveEndIn = PairEndFrames ? PairEndFrames : 4 * PairBlowFrames; // its clip's KillMoveEnd, well after its blow
        }
    for (auto& [cId, actor] : Actors)
        if (actor.KillMoveEndIn && --actor.KillMoveEndIn == 0)
        {
            actor.InKillMove = actor.InPairClip = actor.InPair = false;
            actor.Killer = 0;
        }
    std::vector<uint32_t> killed;
    for (auto& [cId, actor] : Actors)
        if (actor.KillActorIn && --actor.KillActorIn == 0 && actor.InKillMove && !actor.Dead)
            killed.push_back(cId);
    for (const uint32_t cId : killed)
        ClipKillActor(cId);
    // Without the graph's rules, clips loop on each game's own clock; with them, in the graph's step (StepGraph).
    if (Loop && !Graph)
        for (auto& [cId, actor] : Actors)
            StepLoop(actor);
    const uint64_t cFrameTick = World::Get().GetFrameTick();
    const double cFrameMs = FrameMs > 0.0 ? FrameMs : LastFrameTick ? double(cFrameTick - LastFrameTick) : 0.0;
    LastFrameTick = cFrameTick;
    for (auto& [cId, actor] : Actors)
    {
        actor.AnimationStepMs = 0.0;
        // A held pose: its graph takes no update (HookUpdateAnimationGraphs), so it records none either.
        if (!actor.GraphUpdating || PoseHeld(cId))
        {
            actor.Graph.Updated = actor.Graph.Updated && !PoseHeld(cId);
            continue;
        }
        // A recorded owner's is not interpolated (the 63587 hook's interpAllowed), so it steps every frame.
        if (FarOwnerChunkMs <= 0.0 || actor.Copy || World::Get().GetGraphStreams().Recording(cId))
        {
            actor.AnimationStepMs = cFrameMs;
            actor.InterpCountdownMs = 0.0;
        }
        else if ((actor.InterpCountdownMs -= cFrameMs) <= 0.0)
        {
            actor.AnimationStepMs = FarOwnerChunkMs;
            actor.InterpCountdownMs += FarOwnerChunkMs;
        }
        if (actor.AnimationStepMs <= 0.0)
            actor.Graph.Updated = false; // no step this frame: no generate pass either
        actor.AnimationMs += actor.AnimationStepMs;
    }
    // An owner's AI acts on its clip's time, in frames its graph does not update too (a chunked one's end lands on its
    // next chunk).
    if (Graph && OwnerClipEnds)
        for (auto& [cId, actor] : Actors)
        {
            auto& graph = actor.Graph;
            const auto* pClip = Graph->ClipOf(actor.GraphState);
            if (actor.Copy || graph.EndsSent || !graph.ClipStartedAt || !pClip || actor.GraphState != OwnerClipEnds->State)
                continue;
            const double cEnd = *graph.ClipStartedAt + pClip->Ms;
            const bool cChunked = FarOwnerChunkMs > 0.0;
            const double cUntilEnd = cChunked ? (graph.Timeline + FarOwnerChunkMs >= cEnd ? actor.InterpCountdownMs : 1e9)
                                              : cEnd - graph.Timeline;
            if (cUntilEnd > OwnerClipEnds->LeadMs)
                continue;
            graph.EndsSent = true;
            for (const auto& cEvent : OwnerClipEnds->Events)
            {
                ActionEvent end;
                end.ActionId = 0x13000 + uint32_t(cEvent.size());
                end.EventName = cEvent;
                PerformAnimation(cId, end);
            }
        }
    if (Graph)
        for (auto& [cId, actor] : Actors)
        {
            // On its own body (in a pair): off its owner's stream, and back after (the engine's 63587 hook).
            if (auto& streams = World::Get().GetGraphStreams(); actor.Copy && streams.Held(cId))
                streams.HoldWhile(cId, ::ReplayState::CopyOwnBody(actor.InPair, actor.InKillMove, false, SitState(cId)));
            if (actor.Copy)
                World::Get().GetGraphStreams().SeeGraph(cId, GraphIdentity(actor));
            // A copy on the stream: its owner's inputs alone (graph-input-stream.md).
            if (actor.Copy && World::Get().GetGraphStreams().OnStream(cId))
            {
                actor.ExitIn = 0; // its sync point replaced its own clip: no exit event
                if (actor.GraphUpdating && !PoseHeld(cId))
                    ReplayStream(cId, actor);
            }
            else
            {
                // A graph that does not update (kShouldAnimGraphUpdate clear) records nothing for its copies either.
                if (actor.AnimationStepMs > 0.0 && actor.GraphUpdating)
                    UpdateGraph(cId, actor);
                // Its exit clip's own event (42835): standing, off its mount.
                if (actor.ExitIn && !--actor.ExitIn)
                {
                    actor.State1 &= ~(0xFu << kSitShift);
                    EndRide(cId);
                }
            }
        }
    // Each graph's generate pass after every graph's update (63358 -> 63589 -> 63568), when it updated this frame.
    if (Graph)
        for (auto& [cId, actor] : Actors)
        {
            if (!actor.GraphUpdating || !actor.Graph.Updated)
                continue;
            if (auto& streams = World::Get().GetGraphStreams(); !actor.Copy && streams.Recording(cId))
                streams.OnGenerate(cId, streams.Tick());
            GenerateGraph(actor);
        }
    for (auto it = ClipLoads.begin(); it != ClipLoads.end();)
    {
        if (it->Frames-- > 0)
        {
            ++it;
            continue;
        }
        const ClipLoad cLoad = std::move(*it);
        it = ClipLoads.erase(it);
        if (cLoad.Lock && !FakeLoadLock::Live(cLoad.Lock))
        {
            ++ClipLoadHangs; // 63997 spins on the freed lock: Skyrim's frame never ends
            continue;
        }
        if (cLoad.Loaded)
            cLoad.Loaded();
        else if (const auto cActor = Actors.find(cLoad.FormId); cActor != Actors.end())
            cActor->second.LoadedClips = {cLoad.Event};
        cLoad.Done->store(true);
    }
    if (Actors.contains(kPlayer))
        ShovePlayer();
    // Load jobs end (QueuedReference::End 13023, or Finish 13022): a model attaches; then Actor::FinishLoad3D.
    if (!LoadsHeld)
        for (auto& [cId, actor] : Actors)
        {
            if (!std::exchange(actor.LoadQueued, false))
                continue;
            if (std::exchange(actor.ModelReady, false))
            {
                actor.Loaded = true;
                World::Get().GetRunner().Trigger(ReferenceLoadedEvent{cId});
            }
            if (!actor.Refused3D && !(ActorHas3D(cId) && HeldOnGuest(cId)))
                continue;
            if (actor.Refused3D)
                World::Get().GetRunner().Trigger(NpcHeldEvent{cId});
            World::Get().GetRunner().Queue([this, cId] {
                if (Actors.contains(cId))
                    ApplyHeld3D(cId);
            });
        }
    BuildHeads();
    // Getting up (knock states 5 and 6): its ragdoll's bodies stay simulated, driven toward its animation's pose
    // (58291); one this game's world holds stays where it is. Standing, they leave the world.
    for (auto& [cId, actor] : Actors)
    {
        if (!actor.Rising)
            continue;
        const bool cStands = --actor.Rising == 0;
        for (auto& [cKey, body] : Bodies)
        {
            if (body.RagdollOf != cId)
                continue;
            body.Flight.Body.LinearVelocity = {};
            body.Flight.Body.AngularVelocity = {};
            body.Awake = !cStands;
            body.OutOfWorld = cStands;
            body.Held = false;
            if (cStands || body.Caught)
                continue;
            const glm::vec3 cTo = StandingPose(cId, cKey.Index) - body.Flight.Body.Position;
            const float cStep = kRisingSpeed / 60.f;
            const glm::vec3 cMove = glm::length(cTo) > cStep ? glm::normalize(cTo) * cStep : cTo;
            body.Flight.Body.Position += cMove;
            body.Flight.Body.LinearVelocity = cMove * 60.f;
        }
    }
    for (auto& [cId, actor] : Actors)
    {
        // This game's own corpse another game's player moves: its frames drive it too (CopyRagdollPhysics' Played).
        if (!actor.Copy)
        {
            if (actor.Dead && (m_pendingRagdolls.Has(cId) || m_copyRests.contains(cId)))
                DriveCopyRagdoll(cId, m_pendingRagdolls.Take(cId));
            continue;
        }
        if (actor.KnockIn && !--actor.KnockIn)
            actor.KnockedDown = true;
        // After its own physics (CopyRagdollPhysics steers after 58291): lying or getting up, never standing.
        const auto cDrives = m_pendingRagdolls.Take(cId);
        if (actor.Dead || KnockReplay::BodiesSimulated(KnockStateOf(cId)))
            DriveCopyRagdoll(cId, cDrives);
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
    // The sky as 26231 runs it: an aged override dropped first, a change, then the fade's progress (ended last).
    // A guest's held override ages out, mid-fade too; a change (the region's roll, here another weather of its list)
    // starts only once no fade runs. The host's weather is the test's to set.
    if (WeatherHeld && ForcedWeather && ++WeatherAge > kOverrideExpiryFrames)
        ForcedWeather = {};
    if (ForcedWeather && ForcedWeather != Weather && !LastWeather)
        ShowWeather(ForcedWeather);
    else if (!ForcedWeather && WeatherHeld && !LastWeather && WeatherAge > kOverrideExpiryFrames)
    {
        const auto cList = RegionLists.find(WeatherRegion);
        if (cList != RegionLists.end())
            for (const GameId& cRolled : cList->second)
                if (cRolled != Weather)
                {
                    ShowWeather(cRolled);
                    break;
                }
    }
    if (LastWeather && ++FadeFrames >= kWeatherFadeFrames)
        LastWeather = {};
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
    ++DetectionFrame;
    StealthEye();
    ProcessTracking();
    DetectionPass();
    RunQueued();
}

void FakeEngine::Queue(LocalCall aCall, uint32_t aCause, std::function<void(LocalValue)> aOnReturn)
{
    m_queued.push_back({std::move(aCall), aCause, std::move(aOnReturn)});
}

void FakeEngine::RunQueued()
{
    auto queued = std::exchange(m_queued, {});
    for (const auto& [cCall, cCause, cOnReturn] : queued)
    {
        const auto* cpSelf = std::get_if<LocalForm>(&cCall.Self);
        LastReturn = std::monostate{};
        if (cpSelf && QuestFunctions.contains({cpSelf->FormId, cCall.Function}))
            CallsQuest(cCall, cCause, false, false);
        else
            RunScript(cCall, cCause);
        if (cOnReturn)
            cOnReturn(LastReturn);
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
    if (MountBases.contains(aBase) && GraphTransitions.contains({"Standing", "pa_HorseExit"}))
        Actors[cId].GraphState = "Standing"; // MountGraph's
    return cId;
}

void FakeEngine::PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    Actors[aFormId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                            .Essential = EssentialBases.contains(aBase)};
}

void FakeEngine::FinishLoading(uint32_t aFormId)
{
    Actors.at(aFormId).Initializing = false;
    Load3D(aFormId);
}

bool FakeEngine::NpcWalks(uint32_t aFormId, uint32_t aCell, const glm::vec3& acPosition)
{
    auto& actor = Actors.at(aFormId);
    if (actor.Copy || HeldOnGuest(aFormId))
        return false;
    const bool cWasIn = Attached.contains(actor.Cell);
    actor.Cell = aCell;
    std::copy_n(&acPosition.x, 3, actor.Position);
    if (!cWasIn && Attached.contains(aCell))
        Load3D(aFormId);
    return true;
}

void FakeEngine::Load3D(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.Refused3D = HeldOnGuest(aFormId);
    // A job ends in Frame (QueuedReference::End); a load with no job held back attaches at once, as the services
    // see it. Held back, its model waits for Finish (BackgroundLoad3D's result, +0x2C8).
    actor.LoadQueued = actor.Refused3D || LoadsHeld;
    actor.ModelReady = !actor.Refused3D && LoadsHeld;
    actor.Loaded = !actor.Refused3D && !LoadsHeld;
    if (actor.Loaded)
        World::Get().GetRunner().Trigger(ReferenceLoadedEvent{aFormId});
}

void FakeEngine::ApplyHeld3D(uint32_t aFormId)
{
    const auto& cActor = Actors.at(aFormId);
    // Held with 3D: Reload3D, so refused. Not held after a refused load: queued, unless its job is still out.
    if (HeldOnGuest(aFormId) ? ActorHas3D(aFormId) : cActor.Refused3D && !cActor.LoadQueued)
        Load3D(aFormId);
}

bool FakeEngine::HeldOnGuest(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && GuestNpcs::Held(HoldSession, aFormId == kPlayer, it->second.Copy, it->second.ClaimedIn);
}

void FakeEngine::SetCriticalStage(uint32_t aFormId, uint32_t aStage)
{
    auto& actor = Actors.at(aFormId);
    if (!CriticalStage::Takes(aFormId == kPlayer, aStage)) // HookSetCriticalStage
    {
        if (CriticalStage::HidesBody(aFormId == kPlayer, actor.Dead, aStage) && !std::exchange(AshDead, true))
            KeepBodyHidden();
        return;
    }
    actor.CriticalStage = aStage;
    if (!CriticalStage::RemovesBody(aStage))
        return;
    actor.Loaded = false;
    if (aFormId == kPlayer)
        FrameStalled = true;
}

void FakeEngine::StartsAshPileEffect(uint32_t aTarget, uint32_t aEffect)
{
    if (const auto cAsh = AshPileEffects.find(aEffect); cAsh != AshPileEffects.end())
        AshPileUntil[aTarget] = World::Get().GetTick() + cAsh->second;
}

void FakeEngine::KeepBodyHidden()
{
    auto& player = Actors.at(kPlayer);
    if (!CriticalStage::KeepsBodyHidden(AshDead, player.Dead))
    {
        AshDead = false;
        return;
    }
    player.BodyHidden = true;
}

void FakeEngine::ShowFirstPersonRoot(bool aFirstPerson)
{
    auto& player = Actors.at(kPlayer);
    const bool cFirst = player.FirstRootHidden, cThird = player.BodyHidden;
    if (cFirst == cThird || (aFirstPerson ? cFirst : cThird))
    {
        player.BodyHidden = aFirstPerson;
        player.FirstRootHidden = !aFirstPerson;
    }
    KeepBodyHidden(); // HookShowFirstPersonRoot
}

void FakeEngine::CameraEnters(bool aFirstPerson)
{
    CameraFirstPerson = aFirstPerson;
    ShowFirstPersonRoot(aFirstPerson);
}

void FakeEngine::AttachAshPile(uint32_t aFormId)
{
    if (CriticalStage::TakesAshPile(aFormId == kPlayer)) // HookAttachAshPile
        AshPiles.push_back(aFormId);
}

void FakeEngine::CheckHealth(uint32_t aFormId, Actor& aActor) noexcept
{
    if (aActor.Copy || aActor.Dead || aActor.Values.ActorValuesList[kHealth] > 0.f)
        return;
    if (aActor.KillHeld)
    {
        aActor.KillCounted = true; // KillImpl under a deferred kill (37896)
        return;
    }
    (aActor.Essential ? aActor.Bleeding : aActor.Dead) = true;
    if (const auto cAsh = AshPileUntil.find(aFormId); aActor.Dead && cAsh != AshPileUntil.end() && World::Get().GetTick() <= cAsh->second)
    {
        // The ash pile script's OnDying, on whoever holds the effect (its VM runs it on that game).
        SetCriticalStage(aFormId, CriticalStage::kDisintegrateStart);
        AttachAshPile(aFormId);
        SetCriticalStage(aFormId, CriticalStage::kDisintegrateEnd);
    }
    if (aActor.Dead)
    {
        // KillImpl performs the death action (37896): an input of its graph, recorded for its copies, and its graph
        // updates on while it lies dead (Skyrim, duo 2026-11-12: 250 updates in the 250 its living player ran).
        if (auto& streams = World::Get().GetGraphStreams(); Graph && streams.Recording(aFormId) && GraphEventId(kDeathEvent) != UINT16_MAX)
        {
            streams.OnEvent(aFormId, streams.Tick(), GraphEventId(kDeathEvent), GraphWords(aActor), {aActor.GraphRandom, 0});
            TakeGraphEvent(aActor, kDeathEvent);
        }
        else
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
        action.EventName = kDeathEvent;
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
    // A player's copy keeps its 3D in a cell that detached (persistent; Skyrim 2026-11-22, player-copies-after-setpos).
    const bool cKept = it != Actors.end() && it->second.Player && it->second.Copy && it->second.Persistent;
    return it != Actors.end() && it->second.Loaded && !it->second.Disabled &&
           (aFormId == kPlayer || cKept || Attached.contains(it->second.Cell));
}

bool FakeEngine::IsActorFacePainted(uint32_t aFormId) const noexcept
{
    return ActorHas3D(aFormId) && Actors.at(aFormId).HeadBuilt && Actors.at(aFormId).HeadTints.has_value();
}

// Only a head that is built takes paint (FaceGenSystem finds no face before).
bool FakeEngine::PaintActorFace(uint32_t aFormId, const Tints& acTints) noexcept
{
    if (!ActorHas3D(aFormId) || !Actors.at(aFormId).HeadBuilt)
        return false;
    Actors.at(aFormId).HeadTints = acTints;
    return true;
}

void FakeEngine::StartDismount(uint32_t aRider, uint32_t aFrames) noexcept
{
    if (!Mounts.contains(aRider))
        return;
    DismountIn[aRider] = aFrames;
    // The exit pair's start (32803): the mount's half, on its game.
    if (!Actors.at(aRider).Copy && Actors.contains(Mounts.at(aRider)) && !Actors.at(Mounts.at(aRider)).Copy)
        SendGraphEvent(Mounts.at(aRider), "pa_HorseExit");
}

void FakeEngine::MountGraph(FakeEngine& aEngine)
{
    aEngine.GraphTransitions[{"Standing", "pa_HorseExit"}] = kDismountSynced;
    aEngine.GraphTransitions[{kDismountSynced, "PairEnd"}] = "Standing";
    if (!aEngine.Graph)
        aEngine.Graph = FakeGraphRules{};
}

void FakeEngine::SendGraphEvent(uint32_t aFormId, const std::string& acEvent)
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || it->second.GraphState.empty())
        return;
    auto& streams = World::Get().GetGraphStreams();
    if (it->second.Copy && streams.OnStream(aFormId))
        return;
    if (Graph && !it->second.Copy && streams.Recording(aFormId) && GraphEventId(acEvent) != UINT16_MAX)
        streams.OnEvent(aFormId, streams.Tick(), GraphEventId(acEvent), GraphWords(it->second), {it->second.GraphRandom, 0});
    TakeGraphEvent(it->second, acEvent);
}

void FakeEngine::StopRiding(uint32_t aRider, uint32_t aMode, uint32_t aFrames) noexcept
{
    if (!Mounts.contains(aRider))
        return;
    if (aMode == 0)
        DismountIn[aRider] = aFrames;
    else
        EndRide(aRider); // 40352 forced: sit state 0, FinishDismount (38696), Detach
}

// A copy still seated is off at once (its owner got off with no exit clip); one getting off ends by its clip.
void FakeEngine::ActorEndRide(uint32_t aRider) noexcept
{
    if (Actors.contains(aRider) && SitState(aRider) != ::ReplayState::kSitWantsToStand)
        EndRide(aRider);
}

void FakeEngine::ActorSeatOnMount(uint32_t aRider, uint32_t aMount) noexcept
{
    if (!Actors.contains(aRider))
        return;
    ActorPutOnMount(aRider, aMount);
    Actors.at(aRider).State1 = (Actors.at(aRider).State1 & ~(0xFu << kSitShift)) | uint32_t(::ReplayState::kSitSeated) << kSitShift;
    SeatedRiders.insert(aRider);
}

void FakeEngine::SetActorCopy(uint32_t aFormId, bool aCopy) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cReleased = it->second.Copy && !aCopy;
        if (cReleased && it->second.NoiseHeld) // ActorStateRules::HoldsOwnersNoise
            it->second.Noise = 0, it->second.NoiseHeld = false;
        if (aCopy && !it->second.Copy)
            it->second.GraphReady = NewCopiesGraphReady;
        if (aCopy)
            it->second.KillHeld = true; // UpdateCopyAI's StartDeferredKill, each frame
        it->second.Copy = aCopy;
        if (!aCopy)
            it->second.ClaimedIn = HoldSession;
        ApplyHeld3D(aFormId); // SkyrimBridge::SetActorCopy
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
    if (it == Actors.end() || !aBaseFormId || DestroyedNpcs.contains(aBaseFormId))
        return false;
    it->second.Base = aBaseFormId;
    return true;
}

void FakeEngine::SetActorDisplayName(uint32_t aFormId, const std::string& acName) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.DisplayName = PooledNames.try_emplace(NameKey(acName), acName).first->second;
        it->second.CustomName = true;
        it->second.NameEmptied = false;
    }
}

void FakeEngine::ClearAliasNameCaches(uint32_t aQuest)
{
    for (const auto& [cAlias, cRef] : QuestAliasReferences[aQuest])
    {
        const auto cActor = Actors.find(cRef);
        const bool cNamed = (AliasFlags[aQuest][cAlias] & 0x4000) || AliasNameMessages[aQuest].contains(cAlias);
        if (!cNamed || cActor == Actors.end() || cActor->second.DisplayName.empty())
            continue;
        // A message name regenerates; a custom name has nothing to come back from (our hook on 12769 keeps it).
        if (cActor->second.CustomName && DisplayNames::CacheClears(false, false, DisplayNames::kCustomInstance))
            cActor->second.NameEmptied = true;
    }
}

std::string FakeEngine::GetActorDisplayName(uint32_t aFormId) const noexcept
{
    const auto actor = Actors.find(aFormId);
    if (actor != Actors.end() && actor->second.NameEmptied)
        return {};
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
            it->second.InKillMove = false; // KillImpl 37896 clears flags2 0x4000
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

namespace
{
std::set<uint64_t> s_liveLoadLocks;
uint64_t s_nextLoadLock = 0;
} // namespace

FakeLoadLock::FakeLoadLock()
    : Id(++s_nextLoadLock)
{
    s_liveLoadLocks.insert(Id);
}

FakeLoadLock::~FakeLoadLock()
{
    s_liveLoadLocks.erase(Id);
}

bool FakeLoadLock::Live(uint64_t aId) noexcept
{
    return s_liveLoadLocks.contains(aId);
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
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || it->second.Hidden || !it->second.Disabled)
        return;
    it->second.Disabled = false;
    for (const uint32_t cItem : it->second.Outfit)
    {
        const auto& cEntries = it->second.Items.Entries;
        const auto cWorn = std::ranges::find_if(cEntries, [&](const auto& acEntry) { return ::ItemForm(acEntry.BaseId) == cItem && acEntry.ExtraWorn; });
        if (cWorn == cEntries.end())
            continue;
        // The worn list: leveled base, worn, and cannot wear when forced. 16059 expects one of three to survive.
        const bool cExpectedToSurvive = it->second.ForcedWorn.contains(cItem);
        // The unequip strips cannot wear and worn (38920, 16067); what is left merges into an EQUAL unworn list (more
        // of its own stack, or another stack with the same extra data) and is freed. An unequal one leaves it.
        auto unworn = *cWorn;
        unworn.ExtraWorn = unworn.ExtraWornLeft = false;
        const bool cTwin = cWorn->Count > 1 || std::ranges::any_of(cEntries, [&](const auto& acEntry) {
            return &acEntry != &*cWorn && !acEntry.ExtraWorn && !acEntry.ExtraWornLeft && acEntry.CanBeMerged(unworn);
        });
        const int cList = 0, cOther = 0;
        std::vector<const void*> held; // the entry's lists after the unequip
        if (cTwin)
            held = {&cOther};
        else
            held = {&cList};
        // 19694 answers "survived" always; our hooks on it and on 16059 drop a list the entry no longer holds.
        FreedWornList freed;
        freed.Enter();
        freed.Unequipped(&cList, held);
        freed.Enter(); // the nested RemoveItem, handed the list
        WornListsMerged += cTwin;
        if (cExpectedToSurvive && cTwin && !freed.IsFreed(&cList))
            ++WornListDoubleFrees;
        it->second.ForcedWorn.erase(cItem);
    }
}

void FakeEngine::EnableParentLoads(uint32_t aParent, bool aDisabled)
{
    for (auto& [cId, actor] : Actors)
        if (actor.EnableParent == aParent && !(actor.Hidden && !aDisabled))
            actor.Disabled = aDisabled;
}

void FakeEngine::SetActorHidden(uint32_t aFormId, bool aHidden) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Hidden = aHidden;
        it->second.Disabled |= aHidden;
    }
}

void FakeEngine::DisableActor(uint32_t aFormId, bool aDelete) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Disabled = true;
        it->second.Deleted |= aDelete;
        it->second.HeadTints.reset();
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
    Actors[cId].Loaded = MadeActors3D;
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

// As SkyrimBridge's: only a reference made at run time is deleted, never the plugins' or the player.
const void* FakeEngine::GraphManagerKey(Actor& aActor)
{
    if (!aActor.GraphManager)
    {
        // The allocator's freed block first (0xB8 bytes, 63411's SizedDelete); 63355's hook forgets its clip sets.
        const uintptr_t cAddress = FreedGraphManagers.empty() ? (m_lastGraphManager += 0xC0) : FreedGraphManagers.back();
        if (!FreedGraphManagers.empty())
            FreedGraphManagers.pop_back();
        aActor.GraphManager = std::shared_ptr<const void>(reinterpret_cast<const void*>(cAddress), [this](const void* apManager) {
            ClipSets.Forget(apManager);
            FreedGraphManagers.push_back(reinterpret_cast<uintptr_t>(apManager));
        });
    }
    return aActor.GraphManager.get();
}

void FakeEngine::DeleteActor(uint32_t aFormId) noexcept
{
    if (!IsActorTemporary(aFormId) || !Actors.erase(aFormId))
        return;
    m_freedTemporary.insert(aFormId);
    Releasing.insert(aFormId);
}

bool FakeEngine::IsActorNpcTemporary(uint32_t aFormId) const noexcept
{
    return GetActorBaseFormId(aFormId) >= 0xFF000000;
}

bool FakeEngine::IsActorLeveled(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.LeveledBase;
}

// 14375: a new temporary NPC from the pick (its face the pick's).
bool FakeEngine::RelevelActor(uint32_t aFormId, uint32_t aPickId) noexcept
{
    const auto it = Actors.find(aFormId);
    const auto cPick = Npcs.find(aPickId);
    if (it == Actors.end() || !it->second.LeveledBase || cPick == Npcs.end() || DestroyedNpcs.contains(aPickId))
        return false;
    const uint32_t cBuilt = NewTemporary();
    Npcs[cBuilt] = cPick->second;
    Npcs[cBuilt].FaceBase = aPickId;
    Npcs[cBuilt].ChangeFlags = 0;
    it->second.Base = cBuilt;
    return true;
}

void FakeEngine::ResetLeveledActor(uint32_t aFormId, uint32_t aPick)
{
    auto& actor = Actors.at(aFormId);
    if (!actor.LeveledBase || actor.Base == actor.LeveledBase)
        return;
    Npcs.erase(actor.Base);
    DestroyedNpcs.insert(actor.Base);
    actor.Base = actor.LeveledBase;
    RelevelActor(aFormId, aPick);
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
