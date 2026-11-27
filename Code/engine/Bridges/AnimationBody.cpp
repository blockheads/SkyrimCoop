#include <Bridges/AnimationBody.h>
#include <animation/GraphStream.h>
#include <animation/ActionClaims.h>

#include <Animation/TESActionData.h>
#include <animation/ActorMediator.h>
#include <ActionClips.h>
#include <core/Counters.h>

#include <References.h>
#include <animation/KnockReplay.h>
#include <physics/BodyService.h>
#include <animation/ReplayGraph.h>
#include <animation/ActorStateRules.h>
#include <animation/ReplayState.h>
#include <Camera/VATS.h>

#include <PlayerCharacter.h>
#include <Forms/BGSAction.h>
#include <Forms/TESIdleForm.h>
#include <AI/AIProcess.h>
#include <AI/KnockState.h>
#include <Misc/MiddleProcess.h>

#include <Messages/ReferencesMoveRequest.h>

#include <animation/LocalAnimationComponent.h>
#include <animation/RemoteAnimationComponent.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <characters/WaitingFor3D.h>
#include <animation/SpawnGraphStateComponent.h>
#include <core/World.h>
#include <Utils.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <DefaultObjectManager.h>
#include <RTTI.h>
#include <Wire/Buffer.h>
#include <core/FormIds.h>

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

void AnimationBody::YieldPair(World& aWorld, Actor* apCopy, Actor* apOwn) noexcept
{
    apCopy->EndPairs();
    apCopy->EndKillMove();
    apOwn->EndPairs();
    apOwn->EndKillMove();
    apCopy->GetExtension()->ClashPartner = apOwn->formID;
    apCopy->GetExtension()->PairStops = ReplayState::cClashStopUpdates;
    apOwn->GetExtension()->ClashUntil = aWorld.GetFrameTick() + ReplayState::cClashGuardMs;
    // The kill cam of the swing that lost: left running, its end cuts the winner's pair short.
    if (auto* pVats = VATS::Get(); apOwn == PlayerCharacter::Get() && pVats && pVats->mode == VATS::kKillCam)
        pVats->End();
}

bool AnimationBody::PlayReplayedAction(Actor* apActor, const AnimationReplayStep& acStep) noexcept
{
    const auto& cAction = acStep.Action;
    if (ReplayState::HeldByPair(apActor->IsInKillMove(), !cAction.TargetEventName.empty()))
        return true;
    const auto pAction = Cast<BGSAction>(TESForm::GetById(cAction.ActionId));
    const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(cAction.TargetId));

    // Historical action flags keep the copy's current ragdoll and weapon transition.
    const uint32_t cPreviousFlags1 = apActor->actorState.flags1;
    const uint32_t cPreviousFlags2 = apActor->actorState.flags2;
    apActor->actorState.flags1 = ActorStateRules::FromSnapshot1(cAction.State1, cPreviousFlags1);
    apActor->actorState.flags2 = ActorStateRules::FromSnapshot2(cAction.State2, cPreviousFlags2);

    TESActionData actionData(cAction.Type & 0x3, apActor, pAction, pTarget);
    actionData.eventName = BSFixedString(cAction.EventName.c_str());
    // A paired action (a kill move) on an actor of this game plays the partner's half too; a copy partner plays
    // its own, from its owner. So does a mount: getting off hands it back mid-pair (its owner played its half).
    if (acStep.PairHere)
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

void AnimationBody::RunReplayStep(Actor* apActor) noexcept
{
    auto& step = apActor->GetExtension()->Replay;
    RunAnimationReplayStep(step, [&](const AnimationVariables& acVariables) { apActor->LoadAnimationVariables(acVariables); },
                           [&] { return PlayReplayedAction(apActor, step); });
}

namespace
{
Actor* ActorOf(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}
} // namespace

bool AnimationBody::Exists(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId) != nullptr;
}

bool AnimationBody::IsDead(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->IsDead();
}

bool AnimationBody::IsCopy(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->GetExtension()->IsCopy();
}

bool AnimationBody::IsPlayer(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->GetExtension()->IsPlayer();
}

bool AnimationBody::IsMount(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->IsMount();
}

bool AnimationBody::InKillMove(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->IsInKillMove();
}

bool AnimationBody::InPairClip(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->InPairClip();
}

bool AnimationBody::GraphReady(uint32_t aFormId) const noexcept
{
    return EngineVirtual(ActorOf(aFormId)->animationGraphHolder).IsReady();
}

uint8_t AnimationBody::KnockState(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->actorState.GetKnockState();
}

IAnimationBody::State AnimationBody::StateOf(uint32_t aFormId) const noexcept
{
    const auto& cState = ActorOf(aFormId)->actorState;
    return {cState.flags1, cState.flags2};
}

float AnimationBody::Direction(uint32_t aFormId) const noexcept
{
    const Actor* pActor = ActorOf(aFormId);
    return pActor->currentProcess && pActor->currentProcess->middleProcess ? pActor->currentProcess->middleProcess->direction : 0.f;
}

IAnimationBody::Action AnimationBody::KindOf(uint32_t aActionId) const noexcept
{
    if (IsDeathAction(aActionId))
        return Action::kDeath;
    const auto cIs = [&](auto aObject) {
        const auto* pAction = DefaultObjectManager::GetObject(aObject);
        return pAction && pAction->formID == aActionId;
    };
    return cIs(DefaultObjectManager::kActionGetUp) ? Action::kGetUp : cIs(DefaultObjectManager::kActionRagdoll) ? Action::kRagdoll : Action::kOther;
}

AnimationReplayStep& AnimationBody::Step(uint32_t aFormId) noexcept
{
    return ActorOf(aFormId)->GetExtension()->Replay;
}

uint8_t& AnimationBody::PairStops(uint32_t aFormId) noexcept
{
    return ActorOf(aFormId)->GetExtension()->PairStops;
}

uint32_t AnimationBody::ClashPartner(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->GetExtension()->ClashPartner;
}

uint64_t AnimationBody::PoseHeldUntil(uint32_t aFormId) const noexcept
{
    return ActorOf(aFormId)->GetExtension()->PoseHeldUntil.load(std::memory_order_relaxed);
}

void AnimationBody::DriveGraph(uint32_t aFormId) noexcept
{
    ActorOf(aFormId)->flags2 |= Actor::kForceAnimGraphUpdate;
}

void AnimationBody::SetState(uint32_t aFormId, State aState) noexcept
{
    auto& state = ActorOf(aFormId)->actorState;
    state.flags1 = aState.Flags1;
    state.flags2 = aState.Flags2;
}

void AnimationBody::SetLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    ActorOf(aFormId)->GetExtension()->LatestAnimation.Write(acAction);
}

bool AnimationBody::LoadGraphStateFlags(uint32_t aFormId, const std::string& acState) noexcept
{
    return ActorOf(aFormId)->LoadGraphStateFlags(acState);
}

bool AnimationBody::LoadGraphState(uint32_t aFormId, const std::string& acState) noexcept
{
    return ActorOf(aFormId)->LoadAnimationGraphState(acState);
}

void AnimationBody::LoadVariables(uint32_t aFormId, const AnimationVariables& acVariables) noexcept
{
    ActorOf(aFormId)->LoadAnimationVariables(acVariables);
}

std::shared_ptr<std::atomic<bool>> AnimationBody::RequestClips(uint32_t aFormId, const std::string& acEvent) noexcept
{
    return ActionClips::Request(*ActorOf(aFormId), acEvent.c_str());
}

bool AnimationBody::PlayReplayed(uint32_t aFormId, const AnimationReplayStep& acStep) noexcept
{
    return PlayReplayedAction(ActorOf(aFormId), acStep);
}

void AnimationBody::StopPairClip(uint32_t aFormId) noexcept
{
    ActorOf(aFormId)->StopPairClip();
}

void AnimationBody::EnterKillMove(uint32_t aFormId, uint32_t aKiller) noexcept
{
    ActorOf(aFormId)->EnterKillMove(aKiller ? ActorOf(aKiller) : nullptr);
}

void AnimationBody::YieldPair(uint32_t aCopy, uint32_t aOwn) noexcept
{
    YieldPair(World::Get(), ActorOf(aCopy), ActorOf(aOwn));
}

void AnimationBody::KillBesideGraph(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    ActorOf(aFormId)->KillBesideGraph(acAction);
}

bool AnimationBody::KillWithDeathAction(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    return ActorOf(aFormId)->KillWithDeathAction(acAction);
}

void AnimationBody::StandUp(uint32_t aFormId) noexcept
{
    Actor* pActor = ActorOf(aFormId);
    PrepareCopyGetUp(*pActor);
    pActor->actorState.flags1 = (pActor->actorState.flags1 & ~ActorState::kKnockMask) | (5u << 25);
}
