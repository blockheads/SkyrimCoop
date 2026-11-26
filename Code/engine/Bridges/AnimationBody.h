#pragma once

#include <animation/IAnimationBody.h>

struct Actor;
struct World;

// Skyrim's actors as animation playback needs them (sync/animation/AnimationSystem), and the graph job's half.
struct AnimationBody final : IAnimationBody
{
    // At the start of a copy's graph update (its animation job): applies the staged replay step.
    static void RunReplayStep(Actor* apActor) noexcept;
    static bool PlayReplayedAction(Actor* apActor, const AnimationReplayStep& acStep) noexcept;
    // This game's actor ends its pair with a copy (the host gave another pair the claim, or refused this one): both
    // out of the instance and the kill move, its clip stopped until it leaves, its blow dropped, its kill cam ended
    // (docs/re/systems/kill-moves.md, "Yielding").
    static void YieldPair(World& aWorld, Actor* apCopy, Actor* apOwn) noexcept;

    bool Exists(uint32_t aFormId) const noexcept override;
    bool IsDead(uint32_t aFormId) const noexcept override;
    bool IsCopy(uint32_t aFormId) const noexcept override;
    bool IsPlayer(uint32_t aFormId) const noexcept override;
    bool IsMount(uint32_t aFormId) const noexcept override;
    bool InKillMove(uint32_t aFormId) const noexcept override;
    bool InPairClip(uint32_t aFormId) const noexcept override;
    bool GraphReady(uint32_t aFormId) const noexcept override;
    uint8_t KnockState(uint32_t aFormId) const noexcept override;
    State StateOf(uint32_t aFormId) const noexcept override;
    float Direction(uint32_t aFormId) const noexcept override;
    Action KindOf(uint32_t aActionId) const noexcept override;
    AnimationReplayStep& Step(uint32_t aFormId) noexcept override;
    uint8_t& PairStops(uint32_t aFormId) noexcept override;
    uint32_t ClashPartner(uint32_t aFormId) const noexcept override;
    uint64_t PoseHeldUntil(uint32_t aFormId) const noexcept override;
    void DriveGraph(uint32_t aFormId) noexcept override;
    void SetState(uint32_t aFormId, State aState) noexcept override;
    void SetLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept override;
    bool LoadGraphStateFlags(uint32_t aFormId, const std::string& acState) noexcept override;
    bool LoadGraphState(uint32_t aFormId, const std::string& acState) noexcept override;
    void LoadVariables(uint32_t aFormId, const AnimationVariables& acVariables) noexcept override;
    std::shared_ptr<std::atomic<bool>> RequestClips(uint32_t aFormId, const std::string& acEvent) noexcept override;
    bool PlayReplayed(uint32_t aFormId, const AnimationReplayStep& acStep) noexcept override;
    void StopPairClip(uint32_t aFormId) noexcept override;
    void EnterKillMove(uint32_t aFormId, uint32_t aKiller) noexcept override;
    void YieldPair(uint32_t aCopy, uint32_t aOwn) noexcept override;
    void KillBesideGraph(uint32_t aFormId, const ActionEvent& acAction) noexcept override;
    bool KillWithDeathAction(uint32_t aFormId, const ActionEvent& acAction) noexcept override;
    void StandUp(uint32_t aFormId) noexcept override;
};
