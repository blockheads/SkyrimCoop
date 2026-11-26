#pragma once

#include <animation/ReplayState.h>

#include <string_view>

// HookPerformAction's rule with no engine (ActorMediator::PerformAction 38949): whether an actor's action is taken on
// this game, and what follows once the engine answered. An owner's accepted action is recorded and sent to its copies;
// a copy takes only what we force or its half of a pair, and that is never sent. Checked by UnitTests.
namespace ActionRules
{
// Before the engine's call.
struct Asked
{
    bool Copy{};
    bool Forced{};             // an action of ours (a copy converging, a special case)
    bool PlayerActing{};       // this game's player's own action is under way
    bool TargetIsThisPlayer{}; // the action's target is this game's player
    bool Replaying{};          // a replayed action is under way: its owner's game judged it
};

struct Taking
{
    bool Take{};
    // A copy's half of a pair, the local player's or a replayed one: taken, never the owner's own action.
    bool PairHalf{};
    bool operator==(const Taking&) const = default;
};

[[nodiscard]] constexpr Taking Before(const Asked& acAsked) noexcept
{
    const bool cReplayedHalf = acAsked.Replaying && acAsked.Copy;
    const bool cPairHalf = (acAsked.PlayerActing && acAsked.Copy && acAsked.TargetIsThisPlayer) || cReplayedHalf;
    return {.Take = !acAsked.Copy || ReplayState::CopyTakesAction(acAsked.Forced, cPairHalf, cReplayedHalf), .PairHalf = cPairHalf};
}

// After the engine's call.
struct Performed
{
    bool Captured{}; // not a resolved idle query, which picks an animation and never performs it (ReplayCapture)
    bool Accepted{}; // the engine's result
    bool Forced{};
    bool PairHalf{};
    bool Paired{};   // the action names a target event
    bool Streamed{}; // the owner's copies follow its graph stream
    bool OwnBody{};  // ReplayState::CopyOwnBody of the actor
    std::string_view Event;
};

struct Outcome
{
    bool Record{};     // the actor's latest animation, owner or copy
    bool GraphState{}; // the action carries the owner's whole graph state
    bool ResetClash{}; // a new attack: an earlier clash no longer cancels its blow
    bool Send{};       // ActionEvent: the owner's own action, to its copies
    bool operator==(const Outcome&) const = default;
};

[[nodiscard]] constexpr Outcome After(const Performed& acPerformed) noexcept
{
    // A refused action is never replayed: a copy could take it.
    if (!acPerformed.Captured || !acPerformed.Accepted)
        return {};
    const bool cOwn = !acPerformed.Forced && !acPerformed.PairHalf;
    return {.Record = true,
            .GraphState = cOwn && ReplayState::CarriesGraphState(acPerformed.Paired, acPerformed.Streamed, acPerformed.OwnBody),
            .ResetClash = ReplayState::StartsAttack(acPerformed.Event),
            .Send = cOwn};
}
} // namespace ActionRules
