#include <TiltedOnlinePCH.h>

#include <Systems/AnimationSystem.h>

#include <Games/Animation/TESActionData.h>
#include <Games/Animation/ActorMediator.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>

#include <Messages/ClientReferencesMoveRequest.h>

#include <Components.h>
#include <World.h>
#include <Utils.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <DefaultObjectManager.h>

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
                apActor->GetExtension()->LatestAnimation = spawnAction;
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
    if (phase == ReplayStep::kTaken || phase == ReplayStep::kRefused)
    {
        const auto& cAction = step.Action;
        if (phase == ReplayStep::kTaken)
        {
            aAnimationComponent.LastRanAction = cAction;
            apActor->GetExtension()->LatestAnimation = cAction;
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

        // The owner's kill performed this action: the copy dies the same way, on the owner's timeline.
        if (ActorState::LifeStateOf(first.State1) == ActorState::kLifeDying && !apActor->IsDead())
        {
            if (apActor->KillWithDeathAction(first))
            {
                spdlog::debug("Copy {:X} died with its owner's death action {:X} '{}'", apActor->formID, first.ActionId, first.EventName);
                aAnimationComponent.LastRanAction = first;
                apActor->GetExtension()->LatestAnimation = first;
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

        step.Action = first;
        step.Variables = aAnimationComponent.Variables;
        step.Retries = 0;
        aAnimationComponent.StagedUpdates = 0;

        // A paired action (a kill move) on an actor of this game: started here, on the game thread, as its
        // owner's game started it; in the copy's animation job the partner's graph may be updating elsewhere.
        auto* pPartner = first.TargetEventName.empty() ? nullptr : Cast<Actor>(TESForm::GetById(first.TargetId));
        if (pPartner && !pPartner->GetExtension()->IsRemote())
        {
            step.Phase.store(ReplayStep::kBusy, std::memory_order_release);
            apActor->LoadAnimationVariables(step.Variables);
            step.Phase.store(PlayReplayedAction(apActor, step) ? ReplayStep::kTaken : ReplayStep::kRefused, std::memory_order_release);
            return;
        }

        // Applied in step with the copy's graph: variables, one graph update, the action, as on the owner
        // (docs/re/systems/actor-ai-and-copies.md, "Replay in step with the graph").
        step.Phase.store(ReplayStep::kVariables, std::memory_order_release);
    }
}

void AnimationSystem::TargetToNetwork(ActionEvent& aAction) noexcept
{
    const auto cServerId = Utils::GetServerIdByFormId(aAction.TargetId);
    aAction.TargetActor = cServerId ? *cServerId + 1 : 0;
    if (cServerId || (aAction.TargetId >> 24) == 0xFF)
        aAction.TargetId = 0;
}

void AnimationSystem::TargetFromNetwork(ActionEvent& aAction) noexcept
{
    if (aAction.TargetActor)
        aAction.TargetId = Utils::GetFormIdByServerId(aAction.TargetActor - 1);
}

bool AnimationSystem::PlayReplayedAction(Actor* apActor, const ActorExtension::ReplayStep& acStep) noexcept
{
    const auto& cAction = acStep.Action;
    const auto pAction = Cast<BGSAction>(TESForm::GetById(cAction.ActionId));
    const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(cAction.TargetId));

    // The action is played from the owner's state just before it performed it.
    const uint32_t cPreviousFlags1 = apActor->actorState.flags1;
    const uint32_t cPreviousFlags2 = apActor->actorState.flags2;
    apActor->actorState.flags1 = cAction.State1;
    apActor->actorState.flags2 = cAction.State2;

    TESActionData actionData(cAction.Type & 0x3, apActor, pAction, pTarget);
    actionData.eventName = BSFixedString(cAction.EventName.c_str());
    // A paired action (a kill move) on an actor of this game plays the partner's half too; a copy partner plays
    // its own, from its owner.
    auto* pPartner = Cast<Actor>(pTarget);
    if (pPartner && !pPartner->GetExtension()->IsRemote())
        actionData.targetEventName = BSFixedString(cAction.TargetEventName.c_str());
    actionData.idleForm = Cast<TESIdleForm>(TESForm::GetById(cAction.IdleId));
    actionData.someFlag = ((cAction.Type & 0x4) != 0) ? 1 : 0;

    const bool cTaken = ActorMediator::Get()->ForceAction(&actionData);
    if (!cTaken)
    {
        // Refused: the copy keeps its own state flags (the owner's would claim e.g. hands it never drew).
        apActor->actorState.flags1 = cPreviousFlags1;
        apActor->actorState.flags2 = cPreviousFlags2;
    }
    return cTaken;
}

void AnimationSystem::RunReplayStep(Actor* apActor) noexcept
{
    using ReplayStep = ActorExtension::ReplayStep;
    auto& step = apActor->GetExtension()->Replay;
    uint8_t phase = step.Phase.load(std::memory_order_acquire);
    if (phase != ReplayStep::kVariables && phase != ReplayStep::kAction)
        return;
    if (!step.Phase.compare_exchange_strong(phase, ReplayStep::kBusy, std::memory_order_acq_rel))
        return;

    if (phase == ReplayStep::kVariables)
    {
        apActor->LoadAnimationVariables(step.Variables);
        step.Phase.store(ReplayStep::kAction, std::memory_order_release);
        return;
    }
    // As the owner's input retries until its graph takes it, a copy a graph update behind gets 3 more.
    constexpr uint8_t cMaxRetries = 3;
    if (PlayReplayedAction(apActor, step))
        step.Phase.store(ReplayStep::kTaken, std::memory_order_release);
    else
        step.Phase.store(step.Retries++ < cMaxRetries ? ReplayStep::kAction : ReplayStep::kRefused, std::memory_order_release);
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
    auto itor = std::begin(aAnimationComponent.TimePoints);
    const auto end = std::cend(aAnimationComponent.TimePoints);

    auto& lastProcessedAction = aAnimationComponent.LastProcessedAction;

    TiltedPhoques::ViewBuffer buffer((uint8_t*)acActionDiff.data(), acActionDiff.size());
    Buffer::Reader reader(&buffer);

    lastProcessedAction.ApplyDifferential(reader);

    aAnimationComponent.TimePoints.push_back(lastProcessedAction);
}

void AnimationSystem::Serialize(World& aWorld, ClientReferencesMoveRequest& aMovementSnapshot, LocalComponent& localComponent, LocalAnimationComponent& animationComponent, FormIdComponent& formIdComponent)
{
    const auto pForm = TESForm::GetById(formIdComponent.Id);
    const auto pActor = Cast<Actor>(pForm);
    if (!pActor)
        return;

    auto& update = aMovementSnapshot.Updates[localComponent.Id];
    auto& movement = update.UpdatedMovement;

    if (const auto pCell = pActor->parentCell)
        World::Get().GetModSystem().GetServerModId(pCell->formID, movement.CellId.ModId, movement.CellId.BaseId);

    if (const auto pWorldSpace = pActor->GetWorldSpace())
        World::Get().GetModSystem().GetServerModId(pWorldSpace->formID, movement.WorldSpaceId.ModId, movement.WorldSpaceId.BaseId);

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
        localComponent.CurrentAction = latestAction.MoveResult();

    animationComponent.Actions.clear();
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    uint32_t actionBaseId = 0;
    uint32_t actionModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.ActionId, actionModId, actionBaseId))
        return false;

    uint32_t targetBaseId = 0;
    uint32_t targetModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.TargetId, targetModId, targetBaseId))
        return false;

    uint8_t scratch[1 << 14];
    TiltedPhoques::ViewBuffer buffer(scratch, std::size(scratch));
    Buffer::Writer writer(&buffer);
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);

    apData->assign(buffer.GetData(), buffer.GetData() + writer.Size());

    return true;
}
