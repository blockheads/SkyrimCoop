#include <Systems/AnimationSystem.h>

#include <Games/Animation/TESActionData.h>
#include <Games/Animation/ActorMediator.h>
#include <Games/Skyrim/ActionClips.h>

#include <Games/References.h>
#include <Games/KnockReplay.h>
#include <Games/Animation/ReplayGraph.h>
#include <Games/Animation/ReplayState.h>

#include <PlayerCharacter.h>
#include <Forms/BGSAction.h>
#include <Forms/TESIdleForm.h>
#include <AI/AIProcess.h>
#include <AI/KnockState.h>
#include <Misc/MiddleProcess.h>

#include <Messages/ReferencesMoveRequest.h>

#include <Components.h>
#include <World.h>
#include <Utils.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <DefaultObjectManager.h>
#include <RTTI.h>
#include <Wire/Buffer.h>
#include <FormIds.h>

namespace
{
// What KillImpl performs.
bool IsDeathAction(uint32_t aActionId) noexcept
{
    const auto& objects = DefaultObjectManager::Get();
    for (const BGSAction* pAction : {objects.actionDeath, objects.actionDeathWait})
        if (pAction && pAction->formID == aActionId)
            return true;
    return false;
}
} // namespace

extern thread_local const char* g_animErrorCode;

void AnimationSystem::Update(World& aWorld, entt::entity aEntity, Actor* apActor, RemoteAnimationComponent& aAnimationComponent, const uint64_t aTick) noexcept
{
    auto& actions = aAnimationComponent.TimePoints;

    // Off-screen graphs update only now and then (38054); a copy's is driven every frame.
    apActor->flags2 |= Actor::kForceAnimGraphUpdate;

    if (auto* pExtension = apActor->GetExtension(); pExtension->PairStops)
    {
        --pExtension->PairStops;
        for (Actor* pHalf : {apActor, Cast<Actor>(TESForm::GetById(pExtension->ClashPartner))})
            if (pHalf && ReplayState::ResendPairStop(1, pHalf->IsInKillMove(), pHalf->InPairClip()))
                pHalf->StopPairClip();
    }

    // A fresh copy starts in its owner's graph state; the queue waits for it.
    if (const auto* pSpawn = aWorld.try_get<SpawnGraphStateComponent>(aEntity))
    {
        if (aWorld.all_of<WaitingFor3D>(aEntity) || !EngineVirtual(apActor->animationGraphHolder).IsReady())
            return;
        if (apActor->LoadAnimationGraphState(pSpawn->State))
        {
            // The state holds the spawn action's result: it is taken, not replayed.
            if (!actions.empty())
            {
                ActionEvent spawnAction = actions.front();
                TargetFromNetwork(spawnAction);
                spdlog::debug("Copy {:X} starts in its owner's graph state; spawn action {:X} '{}' taken", apActor->formID,
                              spawnAction.ActionId, spawnAction.EventName);
                aAnimationComponent.LastRanAction = spawnAction;
                aAnimationComponent.Variables = spawnAction.Variables;
                aAnimationComponent.VariablesTick = spawnAction.Tick;
                apActor->GetExtension()->LatestAnimation.Write(spawnAction);
                actions.pop_front();
            }
        }
        else
            spdlog::warn("Copy {:X}: the owner's graph state did not load, replaying its last action", apActor->formID);
        aWorld.remove<SpawnGraphStateComponent>(aEntity);
    }

    // The staged step's outcome (RunReplayStep, in the copy's animation job).
    using ReplayStep = ActorExtension::ReplayStep;
    auto& step = apActor->GetExtension()->Replay;
    uint8_t phase = step.Phase.load(std::memory_order_acquire);

    GraphRandom::Track(aAnimationComponent.Random, apActor->GetGraphRandom(), aTick);
    if (aAnimationComponent.CounterUpdates && (apActor->SetGraphCounters(aAnimationComponent.GraphCounters) || !--aAnimationComponent.CounterUpdates))
        aAnimationComponent.CounterUpdates = 0;
    // After the actions before it: played later, they would undo what it loaded.
    if (phase == ReplayStep::kIdle && aAnimationComponent.GraphPoint && (actions.empty() || actions.front().Tick > aAnimationComponent.GraphPoint->Tick))
        RunGraphPoint(apActor, aAnimationComponent, aTick);

    if (phase == ReplayStep::kTaken || phase == ReplayStep::kRefused)
    {
        const auto& cAction = step.Action;
        if (phase == ReplayStep::kTaken)
        {
            aAnimationComponent.LastRanAction = cAction;
            apActor->GetExtension()->LatestAnimation.Write(cAction);
            if (step.Retries)
                spdlog::debug("Copy {:X} took replayed action {:X} '{}' {} graph updates late", apActor->formID, cAction.ActionId,
                              cAction.EventName, step.Retries);
        }
        else
            spdlog::debug("Copy {:X} refused replayed action {:X} '{}' (weapon state {})", apActor->formID, cAction.ActionId,
                          cAction.EventName, apActor->actorState.flags2 >> 5 & 7);
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
    if (phase != ReplayStep::kIdle)
    {
        // Staged for the copy's graph updates; a graph nobody updates gets it here after 10 of ours.
        constexpr uint8_t cMaxStagedUpdates = 10;
        if (++aAnimationComponent.StagedUpdates <= cMaxStagedUpdates || phase == ReplayStep::kBusy)
            return;
        if (!step.Phase.compare_exchange_strong(phase, ReplayStep::kBusy, std::memory_order_acq_rel))
            return;
        spdlog::debug("Copy {:X}: graph not updated, playing {:X} '{}' unstepped", apActor->formID, step.Action.ActionId, step.Action.EventName);
        apActor->LoadAnimationVariables(step.Variables);
        step.Phase.store(PlayReplayedAction(apActor, step) ? ReplayStep::kTaken : ReplayStep::kRefused, std::memory_order_release);
        return;
    }

    const auto it = std::begin(actions);
    if (it != std::end(actions) && it->Tick <= aTick)
    {
        // Check if animation graph is ready before attempting to play animations
        if (!EngineVirtual(apActor->animationGraphHolder).IsReady())
        {
            // Animation graph not ready, keep the action in queue and try again later
            return;
        }

        ActionEvent first = *it;
        TargetFromNetwork(first);
        const auto* pGetUp = DefaultObjectManager::GetObject(DefaultObjectManager::kActionGetUp);
        const bool cGetUp = pGetUp && first.ActionId == pGetUp->formID;
        // The owner's kill performed this action: the copy dies the same way, on the owner's timeline.
        if (ActorState::LifeStateOf(first.State1) == ActorState::kLifeDying && !apActor->IsDead())
        {
            if (apActor->KillWithDeathAction(first))
            {
                spdlog::debug("Copy {:X} died with its owner's death action {:X} '{}'", apActor->formID, first.ActionId, first.EventName);
                aAnimationComponent.LastRanAction = first;
                apActor->GetExtension()->LatestAnimation.Write(first);
            }
            else
                spdlog::debug("Copy {:X}: its kill did not play the owner's death action {:X} '{}'", apActor->formID,
                              first.ActionId, first.EventName);
            actions.pop_front();
            return;
        }
        // A death action of an owner not dying (an essential actor going down, a kill held back): the copy
        // would stay in the death pose while its owner bleeds out or fights on.
        if (IsDeathAction(first.ActionId))
        {
            spdlog::debug("Copy {:X}: skipped its living owner's death action {:X} '{}'", apActor->formID, first.ActionId, first.EventName);
            actions.pop_front();
            return;
        }

        if (first.Tick >= aAnimationComponent.VariablesTick)
        {
            aAnimationComponent.Variables = first.Variables;
            aAnimationComponent.VariablesTick = first.Tick;
        }

        // An action carries its own seed: an older sync point has nothing to add.
        if (aAnimationComponent.GraphPoint && aAnimationComponent.GraphPoint->Tick < first.Tick)
            aAnimationComponent.GraphPoint.reset();

        step.Action = first;
        step.Variables = aAnimationComponent.Variables;
        step.Retries = 0;
        aAnimationComponent.StagedUpdates = 0;

        // BeginGetUp (40150) sets its input and performs the action together. A graph update between them advances
        // the copy's get-up before the action. Paired actions also start here, with a partner this game owns.
        auto* pPartner = first.TargetEventName.empty() ? nullptr : Cast<Actor>(TESForm::GetById(first.TargetId));
        const bool cLocalPartner = pPartner && !pPartner->GetExtension()->IsCopy() && !pPartner->IsMount();
        if (cLocalPartner)
        {
            const ActionEvent cPair = pPartner->GetExtension()->LatestPairAction.Read();
            if (ReplayState::SettlePairedReplay({first.EventName.c_str(), apActor->formID, true, pPartner->IsInKillMove(),
                                                 cPair.TargetEventName.c_str(), cPair.TargetId},
                                                [&](bool aCopy) {
                                                    Actor* pHalf = aCopy ? apActor : pPartner;
                                                    Actor* pOther = aCopy ? pPartner : apActor;
                                                    pHalf->EndPairs();
                                                    pHalf->EndKillMove();
                                                    pHalf->GetExtension()->ClashPartner = pOther->formID;
                                                    pHalf->GetExtension()->ClashUntil = aWorld.GetTick() + ReplayState::cClashGuardMs;
                                                    if (aCopy)
                                                        pHalf->GetExtension()->PairStops = ReplayState::cClashStopUpdates;
                                                }))
            {
                spdlog::debug("Copy {:X}: paired action {:X} '{}' not played, {:X} was in a kill move", apActor->formID, first.ActionId,
                              first.EventName, pPartner->formID);
                aAnimationComponent.LastRanAction = first;
                apActor->GetExtension()->LatestAnimation.Write(first);
                actions.pop_front();
                return;
            }
        }
        if (cGetUp || cLocalPartner)
        {
            step.Phase.store(ReplayStep::kBusy, std::memory_order_release);
            apActor->LoadAnimationVariables(step.Variables);
            step.Phase.store(PlayReplayedAction(apActor, step) ? ReplayStep::kTaken : ReplayStep::kRefused, std::memory_order_release);
            return;
        }

        // Applied in step with the copy's graph: variables, one graph update, the action, as on the owner
        // (docs/re/systems/actor-ai-and-copies.md, "Replay in step with the graph"). Its clips first, as on the owner.
        step.ClipsLoaded = ActionClips::Request(*apActor, first.EventName.c_str());
        step.Phase.store(step.ClipsLoaded ? ReplayStep::kClips : ReplayStep::kVariables, std::memory_order_release);
    }
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

bool AnimationSystem::PlayReplayedAction(Actor* apActor, const ActorExtension::ReplayStep& acStep) noexcept
{
    const auto& cAction = acStep.Action;
    if (ReplayState::HeldByPair(apActor->IsInKillMove(), !cAction.TargetEventName.empty()))
        return true;
    const auto pAction = Cast<BGSAction>(TESForm::GetById(cAction.ActionId));
    const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(cAction.TargetId));

    // Historical action flags keep the copy's current ragdoll and weapon transition.
    const uint32_t cPreviousFlags1 = apActor->actorState.flags1;
    const uint32_t cPreviousFlags2 = apActor->actorState.flags2;
    apActor->actorState.flags1 = (cAction.State1 & ~ActorState::kKnockMask) | (cPreviousFlags1 & ActorState::kKnockMask);
    apActor->actorState.flags2 = ReplayState::Flags2(cAction.State2, cPreviousFlags2);

    TESActionData actionData(cAction.Type & 0x3, apActor, pAction, pTarget);
    actionData.eventName = BSFixedString(cAction.EventName.c_str());
    // A paired action (a kill move) on an actor of this game plays the partner's half too; a copy partner plays
    // its own, from its owner. So does a mount: getting off hands it back mid-pair (its owner played its half).
    auto* pPartner = Cast<Actor>(pTarget);
    if (pPartner && !pPartner->GetExtension()->IsCopy() && !pPartner->IsMount())
        actionData.targetEventName = BSFixedString(cAction.TargetEventName.c_str());
    actionData.idleForm = Cast<TESIdleForm>(TESForm::GetById(cAction.IdleId));
    actionData.someFlag = ((cAction.Type & 0x4) != 0) ? 1 : 0;
    // The action's picks are its owner's: a gesture's, an idle's variant.
    if (cAction.Variables.GraphRandom)
        apActor->SetGraphRandom(cAction.Variables.GraphRandom);

    const bool cGetUp = pAction && pAction == DefaultObjectManager::GetObject(DefaultObjectManager::kActionGetUp);
    const uint8_t cKnockBefore = apActor->actorState.GetKnockState();
    if (KnockReplay::AfterAction(cKnockBefore, cGetUp, true) != cKnockBefore)
    {
        // BeginGetUp (40150) prepares before the action, but changes knock state only after it.
        PrepareCopyGetUp(*apActor);
        apActor->actorState.flags1 = (apActor->actorState.flags1 & ~ActorState::kKnockMask) | (uint32_t(cKnockBefore) << 25);
    }
    const bool cPlayed = cGetUp ? ActorMediator::Get()->PerformAction(&actionData) : ActorMediator::Get()->ForceAction(&actionData);
    const bool cTaken = ReplayGraph::Recover(cPlayed, cAction.GraphState, [&](std::string_view) {
        const bool cLoaded = apActor->LoadAnimationGraphState(cAction.GraphState);
        // The saved state carries the owner's knock flags; our body owns its own ragdoll.
        apActor->actorState.flags1 = (apActor->actorState.flags1 & ~ActorState::kKnockMask) | (cPreviousFlags1 & ActorState::kKnockMask);
        if (cLoaded)
            spdlog::debug("Copy {:X} recovered refused action {:X} '{}' from its owner's graph", apActor->formID,
                          cAction.ActionId, cAction.EventName);
        return cLoaded;
    });
    const uint8_t cKnockAfter = KnockReplay::AfterAction(cKnockBefore, cGetUp, cTaken);
    if (cKnockAfter != cKnockBefore)
        apActor->actorState.flags1 = (apActor->actorState.flags1 & ~ActorState::kKnockMask) | (uint32_t(cKnockAfter) << 25);
    if (!cTaken)
    {
        // Refused: the copy keeps its own state flags (the owner's would claim e.g. hands it never drew).
        apActor->actorState.flags1 = cPreviousFlags1;
        apActor->actorState.flags2 = cPreviousFlags2;
    }
    return cTaken;
}

void AnimationSystem::RunGraphPoint(Actor* apActor, RemoteAnimationComponent& aAnimationComponent, uint64_t aTick) noexcept
{
    const auto& cPoint = *aAnimationComponent.GraphPoint;
    const uint32_t cSeed = cPoint.Variables.GraphRandom;
    const auto cVerdict = GraphRandom::Judge(aAnimationComponent.Random, cSeed, cPoint.Tick, aTick);
    if (cVerdict == GraphRandom::Point::kWait)
        return;
    if (cVerdict == GraphRandom::Point::kLoad && !apActor->IsDead())
    {
        const uint32_t cFlags1 = apActor->actorState.flags1;
        if (apActor->LoadGraphPoint(cSeed, cPoint.GraphState))
        {
            spdlog::debug("Copy {:X} fell in with its owner's graph at a sync point", apActor->formID);
            GraphRandom::Track(aAnimationComponent.Random, cSeed, aTick);
            aAnimationComponent.GraphCounters = GraphRandom::Unpack(cPoint.GraphCounters);
            aAnimationComponent.CounterUpdates = 30;
        }
        // The saved state carries the owner's knock flags; our body owns its own ragdoll.
        apActor->actorState.flags1 = (apActor->actorState.flags1 & ~ActorState::kKnockMask) | (cFlags1 & ActorState::kKnockMask);
    }
    aAnimationComponent.GraphPoint.reset();
}

void AnimationSystem::RunReplayStep(Actor* apActor) noexcept
{
    auto& step = apActor->GetExtension()->Replay;
    RunAnimationReplayStep(step, [&](const AnimationVariables& acVariables) { apActor->LoadAnimationVariables(acVariables); },
                           [&] { return PlayReplayedAction(apActor, step); });
}

void AnimationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<RemoteAnimationComponent>(aEntity))
        aWorld.remove<RemoteAnimationComponent>(aEntity);
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
    const auto pForm = TESForm::GetById(formIdComponent.Id);
    const auto pActor = Cast<Actor>(pForm);
    if (!pActor)
        return;

    auto& update = aMovementSnapshot.Updates[localComponent.Id];
    auto& movement = update.UpdatedMovement;

    if (const auto pCell = pActor->parentCell)
        movement.CellId = ToSession(pCell->formID).value_or(GameId{});

    if (const auto pWorldSpace = pActor->GetWorldSpace())
        movement.WorldSpaceId = ToSession(pWorldSpace->formID).value_or(GameId{});

    movement.Position = pActor->position;

    movement.Rotation.x = pActor->rotation.x;
    movement.Rotation.y = pActor->rotation.z;

    pActor->SaveAnimationVariables(movement.Variables);

    if (pActor->currentProcess && pActor->currentProcess->middleProcess)
    {
        movement.Direction = pActor->currentProcess->middleProcess->direction;
    }

    for (auto& entry : animationComponent.Actions)
    {
        update.ActionEvents.push_back(entry);
    }

    auto latestAction = animationComponent.GetLatestAction();

    if (latestAction)
        localComponent.CurrentAction = std::move(*latestAction);

    const uint64_t cNow = aWorld.GetFrameTick();
    if (!animationComponent.Actions.empty())
        animationComponent.Random.ActionAt = cNow;
    animationComponent.Actions.clear();

    // A pick no action made. Not a player's, a dying actor's or one in a pair: their copies have bodies of their own.
    const bool cPaired = !pActor->GetExtension()->LatestAnimation.Read().TargetEventName.empty();
    if (pActor != PlayerCharacter::Get() && !cPaired && ActorState::LifeStateOf(pActor->actorState.flags1) == ActorState::kLifeAlive &&
        GraphRandom::Picked(animationComponent.Random, pActor->GetGraphRandom(), cNow))
    {
        ActionEvent point;
        point.Tick = cNow;
        point.ActorId = pActor->formID;
        std::vector<GraphRandom::Counter> counters;
        pActor->SaveGraphPoint(point.Variables.GraphRandom, point.GraphState, counters);
        animationComponent.Random.Seed = point.Variables.GraphRandom;
        point.GraphCounters = GraphRandom::Pack(counters);
        if (!point.GraphState.empty())
            update.ActionEvents.push_back(std::move(point));
    }
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer{&buffer};
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);

    apData->assign(reinterpret_cast<const char*>(buffer.GetData()), writer.Size());

    return true;
}
