#include <animation/AnimationSystem.h>

#include <actorstate/DeathActions.h>
#include <animation/ActionClaims.h>
#include <animation/ActorStateRules.h>
#include <animation/GraphStream.h>
#include <animation/IAnimationBody.h>
#include <animation/KnockReplay.h>
#include <animation/ReplayState.h>
#include <animation/SpawnGraphStateComponent.h>
#include <characters/WaitingFor3D.h>
#include <core/FormIds.h>
#include <core/SessionIds.h>
#include <core/World.h>
#include <physics/BodyService.h>
#include <ports/IActorBridge.h>

#include <Messages/ReferencesMoveRequest.h>
#include <Wire/Buffer.h>

namespace
{
bool IsDying(const ActionEvent& acAction) noexcept
{
    return DeathActions::IsDying(acAction.State1);
}
} // namespace

void AnimationSystem::Update(World& aWorld, entt::entity aEntity, const uint32_t aFormId, RemoteAnimationComponent& aAnimationComponent, const uint64_t aTick) noexcept
{
    auto& body = aWorld.ctx().at<IAnimationBody&>();
    if (!body.Exists(aFormId))
        return;
    auto& actions = aAnimationComponent.TimePoints;
    const auto cTaken = [&](const ActionEvent& acAction) {
        aAnimationComponent.LastRanAction = acAction;
        body.SetLatestAnimation(aFormId, acAction);
    };

    body.DriveGraph(aFormId);

    // A yielded pair: its clip has to leave before the pair that won can start.
    if (uint8_t& pairStops = body.PairStops(aFormId); pairStops)
    {
        --pairStops;
        const uint32_t cPlayer = body.ClashPartner(aFormId);
        if (ReplayState::ResendPairStop(1, body.InKillMove(aFormId), body.InPairClip(aFormId)))
            body.StopPairClip(aFormId);
        // A copy's bIsSynced outlasts its clip: only the local player's own clip holds the winner's pair back.
        if (body.Exists(cPlayer) && ReplayState::ResendPairStop(1, body.InKillMove(cPlayer), body.InPairClip(cPlayer)))
        {
            body.StopPairClip(cPlayer);
            if (pairStops)
                return;
        }
    }

    // A fresh copy starts in its owner's graph state; the queue waits for it. On the stream, its sync point is newer:
    // it takes that state's actor state alone (a sit state its owner's procedure set, with no action), and its
    // spawn action is taken as below.
    auto& streams = aWorld.GetGraphStreams();
    if (const auto* pSpawn = aWorld.try_get<SpawnGraphStateComponent>(aEntity); pSpawn && streams.OnStream(aFormId))
    {
        if (body.LoadGraphStateFlags(aFormId, pSpawn->State) && !actions.empty())
        {
            ActionEvent spawnAction = actions.front();
            TargetFromNetwork(spawnAction);
            cTaken(spawnAction);
            actions.pop_front();
        }
        aWorld.remove<SpawnGraphStateComponent>(aEntity);
    }
    if (const auto* pSpawn = aWorld.try_get<SpawnGraphStateComponent>(aEntity))
    {
        if (aWorld.all_of<WaitingFor3D>(aEntity) || !body.GraphReady(aFormId))
            return;
        if (body.LoadGraphState(aFormId, pSpawn->State))
        {
            // The state holds the spawn action's result: it is taken, not replayed.
            if (!actions.empty())
            {
                ActionEvent spawnAction = actions.front();
                TargetFromNetwork(spawnAction);
                spdlog::debug("Copy {:X} starts in its owner's graph state; spawn action {:X} '{}' taken", aFormId, spawnAction.ActionId,
                              spawnAction.EventName);
                aAnimationComponent.Variables = spawnAction.Variables;
                aAnimationComponent.VariablesTick = spawnAction.Tick;
                cTaken(spawnAction);
                actions.pop_front();
            }
        }
        else
            spdlog::warn("Copy {:X}: the owner's graph state did not load, replaying its last action", aFormId);
        aWorld.remove<SpawnGraphStateComponent>(aEntity);
    }

    // On its owner's graph input stream (graph-input-stream.md): its graph's inputs are the stream's, so its actions
    // are bookkeeping only. Its death too: the kill beside the graph is done here, its death's clip is its stream's.
    streams.SetPlayback(aFormId, aTick);
    // Holding its pose for its owner's death: taken as soon as it is heard (ReplayState::TakeOverdueDeath).
    const bool cPoseHeld = ReplayState::PoseHeld(aWorld.GetFrameTick(), body.PoseHeldUntil(aFormId), body.IsDead(aFormId));
    // A stale step still waiting for its clips (ours alone to end) goes with the queue; one in the job plays out first.
    using ReplayStep = AnimationReplayStep;
    auto& step = body.Step(aFormId);
    if (uint8_t waiting = ReplayStep::kClips; cPoseHeld && step.Phase.compare_exchange_strong(waiting, ReplayStep::kIdle))
        aAnimationComponent.StagedUpdates = 0;
    const bool cStepFree = streams.OnStream(aFormId) || step.Phase.load(std::memory_order_acquire) == ReplayStep::kIdle;
    bool deathNow = cStepFree && ReplayState::TakeOverdueDeath(actions, cPoseHeld, IsDying);
    const auto cClaimOf = [&](bool aKillMove, uint64_t aActionTick) {
        return aKillMove ? aWorld.GetActionClaims().Of(Utils::GetSessionIdByFormId(aFormId).value_or(0), aActionTick) : ReplayState::ClaimAnswer::kNotNeeded;
    };
    if (streams.OnStream(aFormId))
    {
        while (!actions.empty() && ReplayState::ActionDue(actions.front().Tick, aTick, deathNow))
        {
            ActionEvent action = actions.front();
            if (IsDying(action) && !body.IsDead(aFormId))
            {
                spdlog::debug("Copy {:X} dies on its owner's stream at its death action {:X} '{}'", aFormId, action.ActionId, action.EventName);
                body.KillBesideGraph(aFormId, action);
            }
            TargetFromNetwork(action);
            // Its owner's pair: with its partner this game's own, or not here, its half is its own body's, by this
            // action, until a point from after the pair. Two copies each stay on their own stream.
            if (!action.TargetEventName.empty())
            {
                const bool cPartner = body.Exists(action.TargetId);
                const bool cKillMove = ReplayState::EntersKillMove(action.EventName);
                const bool cPartnerPlayer = cPartner && body.IsPlayer(action.TargetId);
                const bool cPartnerCopy = cPartner && body.IsCopy(action.TargetId);
                const auto cClaim = cClaimOf(cKillMove, action.Tick);
                // Refused, its claimer ended it; on an NPC still this game's, it waits for the host's answer.
                const auto cOutcome = ReplayState::ClaimOutcome(cClaim, cPartner && ReplayState::ClaimedAway(cKillMove, cPartnerCopy, cPartnerPlayer));
                if (cOutcome == ReplayState::PairOutcome::kWait)
                    return;
                if (cOutcome == ReplayState::PairOutcome::kSkip)
                {
                    actions.pop_front();
                    cTaken(action);
                    continue;
                }
                if (!ReplayState::PairOnStream(cPartnerCopy, ReplayState::TakenVictim(cKillMove, cClaim, cPartnerCopy, cPartnerPlayer)))
                {
                    streams.HoldUntilPoint(aFormId, action.Tick);
                    return;
                }
                // What playing the action does beside the graph (39004): both in the kill move, the victim's killer.
                if (cKillMove && cPartner)
                {
                    body.EnterKillMove(aFormId, 0);
                    body.EnterKillMove(action.TargetId, aFormId);
                }
            }
            actions.pop_front();
            // Its owner's get-up: its own body is prepared and queued to rise; the get-up itself is its graph's, from
            // the stream.
            const auto cKind = body.KindOf(action.ActionId);
            if (KnockReplay::StandsUp(body.KnockState(aFormId), cKind == IAnimationBody::Action::kGetUp))
                body.StandUp(aFormId);
            // Its owner's knock: its own body goes down in the same step as its graph's Ragdoll input (39895).
            if (!body.IsDead(aFormId) && KnockReplay::GoesDown(body.KnockState(aFormId), cKind == IAnimationBody::Action::kRagdoll))
            {
                spdlog::debug("Copy {:X}: down with its owner's ragdoll action", aFormId);
                aWorld.ctx().at<BodyService>().KnockDown(aFormId);
            }
            // Its owner's flags once it took the action (39004's start of an attack, a sit, a recoil), but the record's
            // bits (ActorStateService), its own ragdoll and its own weapon transition.
            const auto cState = body.StateOf(aFormId);
            body.SetState(aFormId, {ActorStateRules::FromSnapshot1(action.StateAfter1, cState.Flags1),
                                    ActorStateRules::FromSnapshot2(action.StateAfter2, cState.Flags2)});
            cTaken(action);
        }
        return;
    }

    // The staged step's outcome (the copy's animation job ran it).
    uint8_t phase = step.Phase.load(std::memory_order_acquire);
    if (phase == ReplayStep::kTaken || phase == ReplayStep::kRefused)
    {
        const auto& cAction = step.Action;
        if (phase == ReplayStep::kTaken)
        {
            cTaken(cAction);
            if (step.Retries)
                spdlog::debug("Copy {:X} took replayed action {:X} '{}' {} graph updates late", aFormId, cAction.ActionId, cAction.EventName,
                              step.Retries);
        }
        else
            spdlog::debug("Copy {:X} refused replayed action {:X} '{}' (weapon state {})", aFormId, cAction.ActionId, cAction.EventName,
                          body.StateOf(aFormId).Flags2 >> 5 & 7);
        if (!actions.empty() && actions.front().Tick == cAction.Tick && actions.front().ActionId == cAction.ActionId)
            actions.pop_front();
        step.Phase.store(ReplayStep::kIdle, std::memory_order_release);
        return;
    }
    if (phase == ReplayStep::kClips)
    {
        if (ClipWaitOver(step, ++aAnimationComponent.StagedUpdates))
        {
            aAnimationComponent.StagedUpdates = 0;
            step.Phase.store(ReplayStep::kVariables, std::memory_order_release);
        }
        return;
    }
    const auto cPlayNow = [&] {
        body.LoadVariables(aFormId, step.Variables);
        step.Phase.store(body.PlayReplayed(aFormId, step) ? ReplayStep::kTaken : ReplayStep::kRefused, std::memory_order_release);
    };
    if (phase != ReplayStep::kIdle)
    {
        // Staged for the copy's graph updates; a graph nobody updates gets it here after 10 of ours.
        constexpr uint8_t cMaxStagedUpdates = 10;
        if (++aAnimationComponent.StagedUpdates <= cMaxStagedUpdates || phase == ReplayStep::kBusy)
            return;
        if (!step.Phase.compare_exchange_strong(phase, ReplayStep::kBusy, std::memory_order_acq_rel))
            return;
        spdlog::debug("Copy {:X}: graph not updated, playing {:X} '{}' unstepped", aFormId, step.Action.ActionId, step.Action.EventName);
        cPlayNow();
        return;
    }

    // Held for its owner's pair: once that action has played here, it may ask for its sync point.
    if (streams.Held(aFormId) &&
        std::ranges::none_of(actions, [&](const ActionEvent& acAction) { return !acAction.TargetEventName.empty() && acAction.Tick <= aTick; }))
        streams.PairPlayed(aFormId);

    if (actions.empty() || !ReplayState::ActionDue(actions.front().Tick, aTick, deathNow))
        return;
    // Its graph is not ready: the action stays first.
    if (!body.GraphReady(aFormId))
        return;

    ActionEvent first = actions.front();
    TargetFromNetwork(first);
    const auto cKind = body.KindOf(first.ActionId);
    // The owner's kill performed this action: the copy dies the same way, on the owner's timeline.
    if (IsDying(first) && !body.IsDead(aFormId))
    {
        if (body.KillWithDeathAction(aFormId, first))
        {
            spdlog::debug("Copy {:X} died with its owner's death action {:X} '{}'", aFormId, first.ActionId, first.EventName);
            cTaken(first);
        }
        else
            spdlog::debug("Copy {:X}: its kill did not play the owner's death action {:X} '{}'", aFormId, first.ActionId, first.EventName);
        actions.pop_front();
        return;
    }
    // A death action of an owner not dying (an essential actor going down, a kill held back): the copy
    // would stay in the death pose while its owner bleeds out or fights on.
    if (cKind == IAnimationBody::Action::kDeath)
    {
        spdlog::debug("Copy {:X}: skipped its living owner's death action {:X} '{}'", aFormId, first.ActionId, first.EventName);
        actions.pop_front();
        return;
    }

    if (first.Tick >= aAnimationComponent.VariablesTick)
    {
        aAnimationComponent.Variables = first.Variables;
        aAnimationComponent.VariablesTick = first.Tick;
    }

    step.Action = first;
    step.Variables = aAnimationComponent.Variables;
    step.Retries = 0;
    aAnimationComponent.StagedUpdates = 0;

    // BeginGetUp (40150) sets its input and performs the action together. A graph update between them advances
    // the copy's get-up before the action. Paired actions also start here, with a partner this game owns.
    const uint32_t cPartnerId = first.TargetId;
    const bool cPartner = !first.TargetEventName.empty() && body.Exists(cPartnerId);
    const bool cKillMove = ReplayState::EntersKillMove(first.EventName);
    const bool cPartnerCopy = cPartner && body.IsCopy(cPartnerId);
    const bool cPartnerPlayer = cPartner && body.IsPlayer(cPartnerId);
    // Refused, its claimer ended it; on an NPC still this game's, it waits for the host's answer.
    const auto cClaim = cClaimOf(cKillMove, first.Tick);
    const auto cClaimOutcome = ReplayState::ClaimOutcome(cClaim, cPartner && ReplayState::ClaimedAway(cKillMove, cPartnerCopy, cPartnerPlayer));
    if (cClaimOutcome == ReplayState::PairOutcome::kWait)
        return;
    if (cClaimOutcome == ReplayState::PairOutcome::kSkip)
    {
        spdlog::debug("Copy {:X}: kill move '{}' not played, its claim refused", aFormId, first.EventName);
        cTaken(first);
        actions.pop_front();
        return;
    }
    const bool cVictimTaken = cPartner && ReplayState::TakenVictim(cKillMove, cClaim, cPartnerCopy, cPartnerPlayer);
    const bool cLocalPartner = cPartner && ReplayState::PairsHere(cPartnerCopy, body.IsMount(cPartnerId), cPartnerPlayer, cKillMove, cVictimTaken);
    step.PairHere = cLocalPartner;
    if (cLocalPartner && !cVictimTaken)
    {
        // A kill move on this game's player goes by the host's answer to its claim (ActionClaims).
        const auto cOutcome = ReplayState::SettlePairedReplay(body.InKillMove(cPartnerId), cClaim);
        if (cOutcome == ReplayState::PairOutcome::kWait)
            return;
        if (cOutcome == ReplayState::PairOutcome::kYield)
        {
            spdlog::debug("{:X}'s pair yields to {:X}'s '{}', the host's claim", cPartnerId, aFormId, first.EventName);
            body.YieldPair(aFormId, cPartnerId);
            return; // the action stays first: played once the clips have left
        }
        if (cOutcome == ReplayState::PairOutcome::kSkip)
        {
            spdlog::debug("Copy {:X}: paired action {:X} '{}' not played on {:X} (refused, or in a pair)", aFormId, first.ActionId, first.EventName,
                          cPartnerId);
            cTaken(first);
            actions.pop_front();
            return;
        }
    }
    if (cKind == IAnimationBody::Action::kGetUp || cLocalPartner)
    {
        step.Phase.store(ReplayStep::kBusy, std::memory_order_release);
        cPlayNow();
        return;
    }

    // Applied in step with the copy's graph: variables, one graph update, the action, as on the owner
    // (docs/re/systems/actor-ai-and-copies.md, "Replay in step with the graph"). Its clips first, as on the owner.
    step.ClipsLoaded = body.RequestClips(aFormId, first.EventName);
    step.Phase.store(step.ClipsLoaded ? ReplayStep::kClips : ReplayStep::kVariables, std::memory_order_release);
}

void AnimationSystem::TargetToNetwork(ActionEvent& aAction) noexcept
{
    const auto cSessionId = Utils::GetSessionIdByFormId(aAction.TargetId);
    aAction.TargetActor = cSessionId ? *cSessionId + 1 : 0;
    if (cSessionId || (aAction.TargetId >> 24) == 0xFF)
        aAction.TargetId = 0;
}

void AnimationSystem::TargetFromNetwork(ActionEvent& aAction) noexcept
{
    if (aAction.TargetActor)
        aAction.TargetId = Utils::GetFormIdBySessionId(aAction.TargetActor - 1);
}

void AnimationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::AddAction(RemoteAnimationComponent& aAnimationComponent, const std::string& acActionDiff) noexcept
{
    auto& lastProcessedAction = aAnimationComponent.LastProcessedAction;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(acActionDiff.data()), acActionDiff.size()));
    lastProcessedAction.ApplyDifferential(reader);
    aAnimationComponent.TimePoints.push_back(lastProcessedAction);
}

void AnimationSystem::Serialize(World& aWorld, ReferencesMoveRequest& aMovementSnapshot, LocalComponent& localComponent, LocalAnimationComponent& animationComponent, FormIdComponent& formIdComponent)
{
    const auto& cActors = aWorld.ctx().at<IActorBridge&>();
    const uint32_t cFormId = formIdComponent.Id;
    if (!cActors.ActorExists(cFormId))
        return;

    auto& update = aMovementSnapshot.Updates[localComponent.Id];
    auto& movement = update.UpdatedMovement;

    if (const uint32_t cCell = cActors.GetActorParentCellFormId(cFormId))
        movement.CellId = ToSession(cCell).value_or(GameId{});
    if (const uint32_t cWorldSpace = cActors.GetActorWorldSpaceFormId(cFormId))
        movement.WorldSpaceId = ToSession(cWorldSpace).value_or(GameId{});

    float x = 0.f, y = 0.f, z = 0.f;
    cActors.GetActorPosition(cFormId, x, y, z);
    movement.Position = {x, y, z};
    cActors.GetActorRotation(cFormId, x, y, z);
    movement.Rotation.x = x;
    movement.Rotation.y = z;

    // A streamed owner's graph variables go in its stream (graph-input-stream.md).
    if (!aWorld.GetGraphStreams().Streaming(cFormId))
        cActors.ActorSaveAnimationVariables(cFormId, movement.Variables);
    movement.Direction = aWorld.ctx().at<IAnimationBody&>().Direction(cFormId);

    for (auto& entry : animationComponent.Actions)
        update.ActionEvents.push_back(entry);
    if (auto latestAction = animationComponent.GetLatestAction())
        localComponent.CurrentAction = std::move(*latestAction);
    animationComponent.Actions.clear();
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer{&buffer};
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);
    apData->assign(reinterpret_cast<const char*>(buffer.GetData()), writer.Size());
    return true;
}
