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

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

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
                const auto& cSpawnAction = actions.front();
                spdlog::debug("Copy {:X} starts in its owner's graph state; spawn action {:X} '{}' taken", apActor->formID,
                              cSpawnAction.ActionId, cSpawnAction.EventName);
                aAnimationComponent.LastRanAction = cSpawnAction;
                aAnimationComponent.Variables = cSpawnAction.Variables;
                aAnimationComponent.VariablesTick = cSpawnAction.Tick;
                apActor->GetExtension()->LatestAnimation = cSpawnAction;
                actions.pop_front();
            }
            aAnimationComponent.HeadVariablesLoaded = false;
        }
        else
            spdlog::warn("Copy {:X}: the owner's graph state did not load, replaying its last action", apActor->formID);
        aWorld.remove<SpawnGraphStateComponent>(aEntity);
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

        const auto& first = *it;

        // The variables go in one update before the action, as on the owner (some actions need the state
        // they lead to, e.g. bWantCastRight -> BeginCastRight).
        if (!aAnimationComponent.HeadVariablesLoaded)
        {
            if (first.Tick >= aAnimationComponent.VariablesTick)
            {
                aAnimationComponent.Variables = first.Variables;
                aAnimationComponent.VariablesTick = first.Tick;
            }
            apActor->LoadAnimationVariables(aAnimationComponent.Variables);
            aAnimationComponent.HeadVariablesLoaded = true;
            return;
        }

        const auto actionId = first.ActionId;
        const auto targetId = first.TargetId;

        const auto pAction = Cast<BGSAction>(TESForm::GetById(actionId));
        const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(targetId));

        // The action is played from the owner's state just before it performed it.
        const uint32_t cPreviousFlags1 = apActor->actorState.flags1;
        const uint32_t cPreviousFlags2 = apActor->actorState.flags2;
        apActor->actorState.flags1 = first.State1;
        apActor->actorState.flags2 = first.State2;

        apActor->LoadAnimationVariables(aAnimationComponent.Variables);

        // Play the animation
        TESActionData actionData(first.Type & 0x3, apActor, pAction, pTarget);
        actionData.eventName = BSFixedString(first.EventName.c_str());
        actionData.idleForm = Cast<TESIdleForm>(TESForm::GetById(first.IdleId));
        actionData.someFlag = ((first.Type & 0x4) != 0) ? 1 : 0;

        const auto result = ActorMediator::Get()->ForceAction(&actionData);
        // A refused action leaves the copy showing something else than its owner (the cause is
        // usually the copy's graph or weapon state, not the network).
        if (result)
        {
            aAnimationComponent.LastRanAction = first;
            apActor->GetExtension()->LatestAnimation = first;
        }
        else
        {
            // Refused: the copy is still in its own state. Keeping the owner's flags would claim
            // e.g. hands drawn on a copy whose draw was refused, and its next sheathe would be
            // refused too (nothing to sheathe).
            apActor->actorState.flags1 = cPreviousFlags1;
            apActor->actorState.flags2 = cPreviousFlags2;
            spdlog::debug("Copy {:X} refused replayed action {:X} '{}' (weapon state {})", apActor->formID, actionId,
                          first.EventName, apActor->actorState.flags2 >> 5 & 7);
        }

        actions.pop_front();
        aAnimationComponent.HeadVariablesLoaded = false;
    }
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
