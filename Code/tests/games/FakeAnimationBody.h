#pragma once

#include "FakeEngine.h"

#include <animation/IAnimationBody.h>

// The fake engine's actors as animation playback needs them (IAnimationBody).
struct FakeAnimationBody final : IAnimationBody
{
    explicit FakeAnimationBody(FakeEngine& aEngine) noexcept : Engine(aEngine) {}

    bool Exists(uint32_t aFormId) const noexcept override { return Engine.ActorExists(aFormId); }
    bool IsDead(uint32_t aFormId) const noexcept override { return Engine.Actors.at(aFormId).Dead; }
    bool IsCopy(uint32_t aFormId) const noexcept override { return Engine.Actors.at(aFormId).Copy; }
    bool IsPlayer(uint32_t aFormId) const noexcept override { return Engine.Actors.at(aFormId).Player; }
    bool IsMount(uint32_t aFormId) const noexcept override { return Engine.IsActorMount(aFormId); }
    bool InKillMove(uint32_t aFormId) const noexcept override { return Engine.Actors.at(aFormId).InKillMove; }
    bool InPairClip(uint32_t aFormId) const noexcept override { return Engine.Actors.at(aFormId).InPairClip; }
    // HoldDeathActions: a test's lever, the copy's replay held back.
    bool GraphReady(uint32_t aFormId) const noexcept override { return Engine.IsAnimationGraphReady(aFormId) && !Engine.HoldDeathActions; }
    uint8_t KnockState(uint32_t aFormId) const noexcept override { return Engine.KnockStateOf(aFormId); }
    State StateOf(uint32_t aFormId) const noexcept override { return {Engine.Actors.at(aFormId).State1, Engine.Actors.at(aFormId).State2}; }
    float Direction(uint32_t) const noexcept override { return 0.f; }
    Action KindOf(uint32_t aActionId) const noexcept override
    {
        return aActionId == FakeEngine::kActionGetUp ? Action::kGetUp : aActionId == FakeEngine::kActionRagdoll ? Action::kRagdoll : Action::kOther;
    }

    AnimationReplayStep& Step(uint32_t aFormId) noexcept override { return Engine.Actors.at(aFormId).Replay; }
    uint8_t& PairStops(uint32_t aFormId) noexcept override { return Engine.Actors.at(aFormId).PairStops; }
    uint32_t ClashPartner(uint32_t aFormId) const noexcept override { return Engine.Actors.at(aFormId).ClashPartner; }
    uint64_t PoseHeldUntil(uint32_t aFormId) const noexcept override { return Engine.Actors.at(aFormId).PoseHeldUntil; }

    void DriveGraph(uint32_t) noexcept override {}
    void SetState(uint32_t aFormId, State aState) noexcept override
    {
        Engine.Actors.at(aFormId).State1 = aState.Flags1;
        Engine.Actors.at(aFormId).State2 = aState.Flags2;
    }
    void SetLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept override { Engine.SetActorLatestAnimation(aFormId, acAction); }
    bool LoadGraphStateFlags(uint32_t aFormId, const std::string& acState) noexcept override { return Engine.LoadGraphStateFlags(aFormId, acState); }
    bool LoadGraphState(uint32_t aFormId, const std::string& acState) noexcept override { return Engine.LoadActorGraphState(aFormId, acState); }
    void LoadVariables(uint32_t aFormId, const AnimationVariables& acVariables) noexcept override
    {
        Engine.ActorLoadAnimationVariables(aFormId, acVariables);
    }
    std::shared_ptr<std::atomic<bool>> RequestClips(uint32_t aFormId, const std::string& acEvent) noexcept override
    {
        return Engine.RequestActionClips(aFormId, acEvent);
    }
    bool PlayReplayed(uint32_t aFormId, const AnimationReplayStep& acStep) noexcept override { return Engine.PlayReplayed(aFormId, acStep.Action); }
    void StopPairClip(uint32_t aFormId) noexcept override { Engine.StopPairClip(aFormId); }
    void EnterKillMove(uint32_t aFormId, uint32_t aKiller) noexcept override
    {
        Engine.Actors.at(aFormId).InKillMove = true;
        if (aKiller)
            Engine.Actors.at(aFormId).Killer = aKiller;
    }
    void YieldPair(uint32_t aCopy, uint32_t aOwn) noexcept override { Engine.ActorYieldPair(aOwn, aCopy); }
    void KillBesideGraph(uint32_t aFormId, const ActionEvent& acAction) noexcept override { Engine.KillBesideGraph(aFormId, acAction); }
    bool KillWithDeathAction(uint32_t aFormId, const ActionEvent& acAction) noexcept override
    {
        Engine.ReplayDeathAction(aFormId, acAction);
        return true;
    }
    void StandUp(uint32_t aFormId) noexcept override { Engine.StandUp(aFormId); }

    FakeEngine& Engine;
};
