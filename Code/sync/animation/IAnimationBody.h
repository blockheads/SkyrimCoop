#pragma once

#include <animation/ReplayStep.h>

#include <cstdint>
#include <memory>
#include <string>

// An actor's body as animation playback needs it (AnimationSystem): the engine's facts and acts, by form id.
// Skyrim's is engine/Bridges/AnimationBody; the tests' is FakeAnimationBody.
struct IAnimationBody
{
    enum class Action : uint8_t { kOther, kDeath, kGetUp, kRagdoll };
    struct State
    {
        uint32_t Flags1{};
        uint32_t Flags2{};
    };

    virtual ~IAnimationBody() = default;

    // Facts.
    [[nodiscard]] virtual bool Exists(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual bool IsDead(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual bool IsCopy(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual bool IsPlayer(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual bool IsMount(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual bool InKillMove(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual bool InPairClip(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual bool GraphReady(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual uint8_t KnockState(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual State StateOf(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual float Direction(uint32_t aFormId) const noexcept = 0;
    // What the engine's default objects say an action is (DefaultObjectManager).
    [[nodiscard]] virtual Action KindOf(uint32_t aActionId) const noexcept = 0;

    // A copy's replay state, kept on its actor: its graph's job reads the step.
    [[nodiscard]] virtual AnimationReplayStep& Step(uint32_t aFormId) noexcept = 0;
    [[nodiscard]] virtual uint8_t& PairStops(uint32_t aFormId) noexcept = 0;
    [[nodiscard]] virtual uint32_t ClashPartner(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual uint64_t PoseHeldUntil(uint32_t aFormId) const noexcept = 0;

    // Acts.
    // Off-screen graphs update only now and then (38054); a copy's is driven every frame.
    virtual void DriveGraph(uint32_t aFormId) noexcept = 0;
    virtual void SetState(uint32_t aFormId, State aState) noexcept = 0;
    virtual void SetLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept = 0;
    // A saved graph state's actor state words alone, or the whole state.
    virtual bool LoadGraphStateFlags(uint32_t aFormId, const std::string& acState) noexcept = 0;
    virtual bool LoadGraphState(uint32_t aFormId, const std::string& acState) noexcept = 0;
    virtual void LoadVariables(uint32_t aFormId, const AnimationVariables& acVariables) noexcept = 0;
    // The action's clips asked for, as on the owner; null when there is nothing to wait for.
    virtual std::shared_ptr<std::atomic<bool>> RequestClips(uint32_t aFormId, const std::string& acEvent) noexcept = 0;
    virtual bool PlayReplayed(uint32_t aFormId, const AnimationReplayStep& acStep) noexcept = 0;
    virtual void StopPairClip(uint32_t aFormId) noexcept = 0;
    // Both halves of a pair in the kill move; a victim's killer is aKiller, 0 for the killer itself.
    virtual void EnterKillMove(uint32_t aFormId, uint32_t aKiller) noexcept = 0;
    // This game's actor ends its pair with a copy (docs/re/systems/kill-moves.md, "Yielding").
    virtual void YieldPair(uint32_t aCopy, uint32_t aOwn) noexcept = 0;
    // The kill beside the graph (its death's clip is its stream's), or with its owner's death action.
    virtual void KillBesideGraph(uint32_t aFormId, const ActionEvent& acAction) noexcept = 0;
    virtual bool KillWithDeathAction(uint32_t aFormId, const ActionEvent& acAction) noexcept = 0;
    // Its own body prepared and queued to rise (BeginGetUp 40150, knock state 5).
    virtual void StandUp(uint32_t aFormId) noexcept = 0;
};
