#include "FakeEngine.h"
#include <actorstate/AiStateChangeEvent.h>
#include <core/World.h>
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
#include <magic/EffectVisualsChangeEvent.h>
#include <items/EquipmentChangeEvent.h>
#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDamagedEvent.h>
#include <characters/ActorValueChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <animation/ReplayCapture.h>
#include <animation/ReplayGraph.h>
#include <animation/ActionRules.h>
#include <magic/ProjectileLaunchedEvent.h>
#include <combat/TrapRules.h>
#include <animation/ActorStateRules.h>
#include <animation/ReplayState.h>

using namespace PartyRules;

void FakeEngine::RunNativeCombat() noexcept
{
    for (const auto& [cActor, cCandidates] : CombatCandidates)
    {
        for (uint32_t selection = 0; selection < CombatSelectionsPerFrame; ++selection)
        {
            const auto it = Actors.find(cActor);
            if (it == Actors.end() || it->second.Copy || it->second.Dead || !it->second.Ai.CombatState)
                continue;
            auto& ai = it->second.Ai;
            uint32_t cNative = CombatProposals.contains(cActor) ? CombatProposals.at(cActor) : ai.CombatTargetId;
            auto candidates = cCandidates;
            // ReactToDetection (39765) -> 39747 -> assault 39359 -> StartCombat 38561 -> AddTarget 44704: a friend
            // joins the group once the NPC's reaction makes it an enemy, and stays.
            std::erase_if(CombatFriendly, [&](uint64_t aKey) {
                return uint32_t(aKey >> 32) == cActor && PartyThreats.IsPartyFoe(cActor, uint32_t(aKey));
            });
            std::erase_if(candidates, [&](const PartyThreat::Candidate& acCandidate) {
                return CombatFriendly.contains(PartyThreat::State::Key(cActor, acCandidate.ActorId));
            });
            // 47191: a dead cached target fails its check, so 47195 picks again at once; the party rule skips the dead.
            for (auto& candidate : candidates)
                if (Actors.contains(candidate.ActorId) && IsActorDead(candidate.ActorId))
                    candidate.Eligible = false;
            // Only when the pick (a proposal or the cached target) is itself dead.
            if (Actors.contains(cNative) && IsActorDead(cNative))
                for (const auto& cCandidate : candidates)
                    if (cCandidate.Eligible)
                    {
                        cNative = cCandidate.ActorId;
                        break;
                    }
            for (auto& candidate : candidates)
                if (const auto sight = CombatPerception.find(PartyThreat::State::Key(cActor, candidate.ActorId));
                    sight != CombatPerception.end())
                    candidate.Eligible &=
                        PartyThreats.CanTarget(cActor, candidate.ActorId, ai.CombatTargetId, sight->second);
            const uint32_t cChosen = PartyThreats.Resolve(cActor, ai.CombatTargetId, cNative, candidates);
            if (cChosen != ai.CombatTargetId)
            {
                ai.CombatTargetId = cChosen;
                World::Get().GetRunner().Trigger(AiStateChangeEvent{cActor});
            }
        }
    }
}

void FakeEngine::ActorHoldKill(uint32_t aFormId, bool aHold) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    it->second.KillHeld = aHold;
    // Released: the counted kill dropped first, so 40036 queues none (Actor::HoldKill).
    if (!aHold)
        it->second.KillCounted = false;
}

void FakeEngine::KillActorBy(uint32_t aFormId, uint32_t aKillerFormId) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || it->second.Dead)
        return;
    it->second.KillHeld = it->second.KillCounted = false;
    it->second.PoseHeldUntil = 0;
    it->second.KilledBy = aKillerFormId;
    it->second.Values.ActorValuesList[kHealth] = 0.f;
    CheckHealth(aFormId, it->second);
}

void FakeEngine::ActorYieldPair(uint32_t aOwnFormId, uint32_t aCopyFormId) noexcept
{
    if (!Actors.contains(aOwnFormId) || !Actors.contains(aCopyFormId))
        return;
    for (const uint32_t cHalf : {aCopyFormId, aOwnFormId})
    {
        StopPairClip(cHalf);
        EndKillMove(cHalf);
    }
    Actors.at(aCopyFormId).ClashPartner = aOwnFormId;
    Actors.at(aCopyFormId).PairStops = ::ReplayState::cClashStopUpdates;
    Actors.at(aOwnFormId).ClashUntil = World::Get().GetFrameTick() + ::ReplayState::cClashGuardMs;
}

bool FakeEngine::ActorPerformPlainly(uint32_t aFormId, uint32_t aActionId) noexcept
{
    ActionEvent swing;
    swing.ActionId = aActionId;
    swing.EventName = kPlainSwing;
    if (!Actors.contains(aFormId) || !PerformAnimation(aFormId, swing))
        return false;
    ++PlainSwings;
    return true;
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
    if (actor.Copy)
        actor.ReplayedOn = actor.GraphVariables;
    if (const auto cPartner = Actors.find(acAction.TargetId); !acAction.TargetEventName.empty() && cPartner != Actors.end())
    {
        if (!(actor.InKillMove && cPartner->second.InKillMove) && !::ReplayState::PairMarkClear(!PairMarkBlocked, actor.Copy))
            return false;
        // An uninterruptible transition still playing drops the new pair's event too.
        if ((actor.InPairClip && actor.StopsDropped) || (cPartner->second.InPairClip && cPartner->second.StopsDropped))
            return false;
        // 32803 makes an instance and plays the target's half through its graph (63591); a replay names no target
        // event for a copy partner (PlayReplayedAction): no instance, its half is its owner's action.
        const bool cInstance = !Replaying || actor.Replay.PairHere;
        // A copy on its owner's stream takes the half only held off the stream (the engine's 63591 hook); refused,
        // the pair fails.
        if (auto& streams = World::Get().GetGraphStreams();
            cInstance && cPartner->second.Copy && streams.OnStream(acAction.TargetId) && !streams.HoldWhile(acAction.TargetId, true))
            return false;
        // 32803 plays the partner's half through Func5 (41557) -> 38949: our hook refuses a copy's own action but for
        // the local player's partner's half or a half of a pair replayed here; refused, the pair fails.
        if (cInstance && cPartner->second.Copy &&
            !ActionRules::Before({.Copy = true, .PlayerActing = !Replaying && aFormId == kPlayer, .TargetIsThisPlayer = aFormId == kPlayer, .Replaying = Replaying}).Take)
            return false;
        // 32803 takes a pair on two actors already paired as the same pair; the second clip ends both at once. Paired:
        // in an instance (32808), or each playing its half from its own game's pair; not the kill move flag alone (an
        // actor-state record sets that, 38613).
        const bool cOtherHalf = cPartner->second.PairHalf.EventName == acAction.TargetEventName &&
                                cPartner->second.PairHalf.TargetEventName == acAction.EventName;
        const bool cCollapsed = !cOtherHalf && (actor.InPair || actor.InPairClip) && (cPartner->second.InPair || cPartner->second.InPairClip);
        CollapsedPairs += cCollapsed;
        // Only a "pa_Kill" action puts both in a kill move (39004); a mount's pair leaves that alone.
        if (cCollapsed || ::ReplayState::EntersKillMove(acAction.EventName))
        {
            actor.InKillMove = cPartner->second.InKillMove = !cCollapsed;
            cPartner->second.Killer = cCollapsed ? 0 : aFormId; // 38613 on the victim, the attacker its killer
        }
        actor.PairHalf = acAction;
        if (!cCollapsed && !cOtherHalf)
        {
            cPartner->second.PairHalf.EventName = acAction.TargetEventName;
            cPartner->second.PairHalf.TargetEventName = acAction.EventName;
        }
        // The victim state's AddRagdollToWorld (AddRagdollHandler 42873): its ragdoll in the world, biped layer.
        if (cPartner->second.InKillMove)
        {
            cPartner->second.BodiesLayer = kBipedLayer;
            cPartner->second.DynamicBodies = ~1u; // driveToPose (58291): its limbs dynamic, its root body keyframed
        }
        if (PairBlows && !cCollapsed && !actor.Copy && acAction.EventName.starts_with("pa_"))
        {
            actor.PairBlowIn = PairBlowFrames;
            actor.PairVictim = acAction.TargetId;
        }
        // The victim's half plays here (an instance): its own clip's KillActor, at the blow's time.
        if (PairBlows && cInstance && !cCollapsed && !cOtherHalf && acAction.EventName.starts_with("pa_"))
            cPartner->second.KillActorIn = PairBlowFrames;
        actor.InPairClip = cPartner->second.InPairClip = !cCollapsed;
        if (cInstance || cCollapsed)
        {
            actor.InPair = cPartner->second.InPair = !cCollapsed;
            actor.MarkWaitMs = cPartner->second.MarkWaitMs = PairMarkMs; // each walks to its mark first (32792)
        }
        // The victim's half is its own action (TESActionData::Func5 41557 under Animation.cpp's hook): an owner's is
        // sent, and its graph takes the event in the instance.
        if (cInstance && !cCollapsed && !Replaying && !cPartner->second.Copy)
        {
            ActionEvent half = acAction;
            half.ActorId = acAction.TargetId;
            half.TargetId = aFormId;
            half.EventName = acAction.TargetEventName;
            half.TargetEventName = acAction.EventName;
            half.Variables = cPartner->second.GraphVariables;
            half.Variables.GraphRandom = cPartner->second.GraphRandom;
            half.GraphState.clear();
            if (!GraphTransitions.empty() && !cPartner->second.GraphState.empty())
            {
                if (auto& streams = World::Get().GetGraphStreams(); Graph && streams.Recording(half.ActorId) && streams.HoldPointsWhile(half.ActorId, true))
                    streams.OnEvent(half.ActorId, streams.Tick(), GraphEventId(half.EventName), GraphWords(cPartner->second), {cPartner->second.GraphRandom, 0});
                TakeGraphEvent(cPartner->second, half.EventName);
            }
            cPartner->second.LatestAnimation = half;
            World::Get().GetDispatcher().trigger(half);
        }
        actor.PairedWithCopy = cPartner->second.Copy;
        cPartner->second.PairedWithCopy = actor.Copy;
        actor.StopsDropped = cPartner->second.StopsDropped = PairStopsDropped;
    }
    else if (actor.Copy && actor.InKillMove)
        ++BrokenPairs;
    const auto cWeaponBefore = actor.Weapon;
    if (actor.Copy)
    {
        actor.Weapon = WeaponState((ActorStateRules::FromSnapshot2(acAction.State2, uint32_t(actor.Weapon) << 5) >> 5) & 7);
        actor.State1 = ActorStateRules::FromSnapshot1(acAction.State1, actor.State1);
    }
    if (const auto cState = ActionStates.find(acAction.EventName); cState != ActionStates.end())
        actor.State1 = cState->second.second ? actor.State1 | cState->second.first : actor.State1 & ~cState->second.first;
    if (const auto cExit = ExitClips.find(acAction.EventName); cExit != ExitClips.end())
    {
        actor.State1 = (actor.State1 & ~(0xFu << kSitShift)) | uint32_t(::ReplayState::kSitWantsToStand) << kSitShift;
        actor.ExitIn = cExit->second;
    }
    if (!GraphTransitions.empty() && !actor.GraphState.empty())
    {
        // 63591's hook: an owner's event recorded before its graph handles it (engine/Animation).
        if (auto& streams = World::Get().GetGraphStreams(); Graph && !actor.Copy && streams.Recording(aFormId))
            streams.OnEvent(aFormId, streams.Tick(), GraphEventId(acAction.EventName), GraphWords(actor), {actor.GraphRandom, 0});
        // A copy on its stream takes its owner's events alone (63591's hook drops any other).
        const bool cDropped = actor.Copy && World::Get().GetGraphStreams().OnStream(aFormId);
        if (cDropped || !TakeGraphEvent(actor, acAction.EventName))
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
    }
    return true;
}

uint32_t FakeEngine::DrawGraphRandom(Actor& aActor) noexcept
{
    return aActor.GraphRandom = aActor.GraphRandom * 1664525 + 1013904223;
}

void FakeEngine::LoadMovementClips(uint32_t aFormId, const std::string& acEvent)
{
    if (auto& streams = World::Get().GetGraphStreams(); !Actors.at(aFormId).Copy && streams.Recording(aFormId))
        streams.OnClips(aFormId, streams.Tick(), 47, GraphEventId(acEvent));
}

std::shared_ptr<std::atomic<bool>> FakeEngine::RequestMovementClips(uint32_t aFormId, const std::string& acEvent,
                                                                   std::shared_ptr<std::atomic<bool>> aDone)
{
    ClipRequestsUnderGraphLock += m_replaying;
    const auto cActor = Actors.find(aFormId);
    if (cActor == Actors.end() || (!ClipFiles.contains(acEvent) && !OtherSetClips.contains(acEvent)))
        return nullptr;
    auto done = aDone ? std::move(aDone) : std::make_shared<std::atomic<bool>>(false);
    // As ActionClips: the holder's lock and callback; the holder goes once none of its loads is queued.
    void* pLoad = MovementSets.Request(
        aFormId, [this](uint32_t aId) { return !Actors.contains(aId); },
        [](uint32_t aNumber) { return reinterpret_cast<void*>(uintptr_t(aNumber)); },
        [&](auto& aHolder, void* apCallback) {
            const uint32_t cNumber = uint32_t(reinterpret_cast<uintptr_t>(apCallback));
            ClipLoads.push_back({aFormId, acEvent, ClipLoadFrames, done, aHolder.LoadLock.Id,
                                 [this, aFormId, acEvent, cNumber] {
                                     MovementSets.Loaded(aFormId, cNumber, [&](MovementSet& aSet) { aSet.Event = acEvent; });
                                 }});
            return reinterpret_cast<void*>(++m_nextClipLoad); // the load, as 43621 returns it
        },
        [](void*) {}, [](MovementSet&) {});
    return pLoad ? done : nullptr;
}

std::shared_ptr<std::atomic<bool>> FakeEngine::RequestActionClips(uint32_t aFormId, const std::string& acEvent,
                                                                 std::shared_ptr<std::atomic<bool>> aDone)
{
    ClipRequestsUnderGraphLock += m_replaying;
    if (!ClipFiles.contains(acEvent) && !OtherSetClips.contains(acEvent))
        return nullptr;
    auto done = aDone ? std::move(aDone) : std::make_shared<std::atomic<bool>>(false);
    ClipLoads.push_back({aFormId, acEvent, ClipLoadFrames, done});
    return done;
}

bool FakeEngine::PerformAnimation(uint32_t aFormId, ActionEvent aAction, bool aQuery)
{
    aAction.State2 = (aAction.State2 & ~ActorStateRules::kWeaponMask) | Actors.at(aFormId).State2 | (uint32_t(Actors.at(aFormId).Weapon) << 5);
    aAction.State1 |= Actors.at(aFormId).State1;
    uint32_t flags = aQuery ? 1 : 0;
    aAction.Type = 2 | (flags ? 4 : 0);
    aAction.Tick = World::Get().GetFrameTick();
    aAction.ActorId = aFormId;
    aAction.Variables = Actors.at(aFormId).GraphVariables; // SaveAnimationVariables, as Animation.cpp's hook
    aAction.Variables.GraphRandom = Actors.at(aFormId).GraphRandom;
    if (ClipFiles.contains(aAction.EventName))
    {
        // Its process loads the action's clip set first (39625), in its stream's place.
        Actors.at(aFormId).LoadedClips = {aAction.EventName};
        ClipSets.Record(GraphManagerKey(Actors.at(aFormId)), 3, aAction.EventName.c_str()); // 43621's hook
        if (auto& streams = World::Get().GetGraphStreams(); !Actors.at(aFormId).Copy && streams.Recording(aFormId))
            streams.OnClips(aFormId, streams.Tick(), 3, GraphEventId(aAction.EventName));
    }
    const auto cOutcome = ReplayCapture::Run(flags, [&]() -> uint8_t {
        // PerformAction (38949) resolves a query, changes flag 1 to 2 and never sends its event.
        if (flags & 1)
        {
            flags = 2;
            return 1;
        }
        return ReplayAnimation(aFormId, aAction);
    });
    if (!aQuery)
        ApplyActionWrites(aFormId, aAction.EventName);
    // HookPerformAction's second half: ActionRules::After decides.
    const auto cAfter = ActionRules::After({
        .Captured = cOutcome.Capture,
        .Accepted = cOutcome.Result != 0,
        .Paired = !aAction.TargetEventName.empty(),
        // Without a behaviour graph no copy is on a stream (a fixture's scripted transitions alone).
        .Streamed = Graph && World::Get().GetGraphStreams().Streaming(aFormId),
        .OwnBody = ::ReplayState::CopyOwnBody(false, false, false, SitState(aFormId)),
        .Event = aAction.EventName,
    });
    if (cAfter.Record)
        aAction.StateAfter1 = Actors.at(aFormId).State1;
    if (cAfter.ResetClash)
        Actors.at(aFormId).ClashUntil = 0;
    if (cAfter.GraphState)
        aAction.GraphState = SaveActorGraphState(aFormId);
    if (cAfter.Record)
        Actors.at(aFormId).LatestAnimation = aAction;
    if (cAfter.Send)
        World::Get().GetDispatcher().trigger(aAction);
    return cOutcome.Result != 0;
}

bool FakeEngine::DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    // A forwarded hit on an actor a kill move holds (SkyrimBridge::DamageActorHealth: in its kill move, 38619): its
    // clip's KillActor kills it. A hit seen here goes through the engine's own damage (TakesDamage).
    if (!m_applyingOwnHit && it->second.InKillMove && it->second.Killer)
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

void FakeEngine::TakeTrapHit(uint32_t aFormId, uint32_t, const Payloads::TrapHit& acHit) noexcept
{
    TakesDamage(aFormId, 0, acHit.Damage, false);
}

void FakeEngine::Strikes(uint32_t aHitter, uint32_t aHittee, float aDamage)
{
    if (!Actors.contains(aHittee))
        return;
    TakesDamage(aHittee, aHitter, aDamage, false, false, ::ReplayState::CancelledBlow(World::FrameTick() < Actors.at(aHitter).ClashUntil));
}

void FakeEngine::Shoots(uint32_t aShooter, uint32_t aSpell, uint32_t aProjectile, const glm::vec3& acOrigin)
{
    ProjectileLaunchedEvent event{};
    event.Origin = acOrigin;
    event.ShooterID = aShooter;
    event.SpellID = aSpell;
    event.ProjectileBaseID = aProjectile;
    event.ParentCellID = Actors.at(aShooter).Cell;
    if (TrapRules::SendsLaunch(Spell(aSpell).Concentration, true)) // HookLaunch (44108)
        World::Get().GetRunner().Trigger(event);
    Shots.push_back({aShooter, aSpell, acOrigin});
}

void FakeEngine::BodyImpacts(uint32_t aTarget, float aDamage)
{
    if (HitRules::JudgesImpact(PartyOf(aTarget)))
        TakesDamage(aTarget, 0, aDamage, false, false);
}

void FakeEngine::ObjectProjectileHits(uint32_t aTarget, float aDamage, uint32_t aEnchantment, uint32_t aEffect, float aMagnitude)
{
    // Projectile::Func190 (44204) with no actor shooter: a HitData with no aggressor (ProcessHit 38586), a world hit.
    TakesDamage(aTarget, 0, aDamage, false, true);
    if (!aEnchantment)
        return;
    InWorldImpact = true;
    SpellHits(aTarget, aTarget, aEnchantment, aEffect, aMagnitude);
    InWorldImpact = false;
}

void FakeEngine::ObjectShoots(uint32_t aObject, uint32_t aSpell, uint32_t aProjectile, uint32_t aCell,
                              const glm::vec3& acOrigin, uint32_t aWeapon)
{
    ProjectileLaunchedEvent event{};
    event.Origin = acOrigin;
    event.ShooterID = aObject;
    event.SpellID = aSpell;
    event.ProjectileBaseID = aProjectile;
    event.WeaponID = aWeapon;
    event.ParentCellID = aCell;
    event.FromObject = true;
    if (TrapRules::SendsLaunch(Spell(aSpell).Concentration, false)) // HookLaunch (44108)
        World::Get().GetRunner().Trigger(event);
    Shots.push_back({aObject, aSpell, acOrigin});
}

void FakeEngine::Activates(uint32_t aActor, uint32_t aObject)
{
    ActivateEvent event;
    event.ObjectFormId = aObject;
    event.ActivatorFormId = aActor;
    event.ObjectParentCellFormId = Actors.contains(aObject) ? Actors.at(aObject).Cell : References.at(aObject).Cell;
    if (aActor == kPlayer && References.contains(aObject) && References.at(aObject).Lever && !References.at(aObject).ActivationBlocked)
        event.FurnitureEvent = kPull;
    World::Get().GetRunner().Trigger(event);
    Activations.emplace_back(aObject, aActor);
    if (ActivateMount(aActor, aObject))
        return;
    // The player activating a corpse opens its loot (TESNPC::Activate -> ContainerMenu::OpenMenu, our hook first).
    if (aActor == kPlayer && Actors.contains(aObject) && IsActorDead(aObject) && !LootMenuOf &&
        !ContainerOpens::Refused(Actors.at(aObject).Copy && Actors.at(aObject).Player))
    {
        LootMenuOf = aObject;
        World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    }
    // Only this game's player toggles a chest's lid (TESObjectCONT::Activate 17887, OpenOrClose 14284); a copy's
    // replayed activation leaves it.
    if (const auto cChest = References.find(aObject); aActor == kPlayer && cChest != References.end() && cChest->second.Chest)
    {
        cChest->second.Open = true;
        cChest->second.Lid = 1.f;
        ChestMenuOf = aObject;
        if (!IsGuest) // the host's open state change, sent as a row
            Recorded.push_back({.Type = ObjectChange::Kind::kOpenState, .Object = SessionIdOf(aObject), .Value = 1});
        World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    }
    ToggleDoor(aObject);
    Harvest(aObject);
    if (aActor == kPlayer)
        PullsLever(aObject);
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
