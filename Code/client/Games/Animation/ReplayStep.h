#pragma once

#include <Games/DeathActions.h>
#include <Structs/ActionEvent.h>
#include <atomic>
#include <cstdint>
#include <memory>

struct AnimationReplayStep
{
    enum : uint8_t { kIdle, kClips, kVariables, kAction, kBusy, kTaken, kRefused };
    std::atomic<uint8_t> Phase{kIdle};
    uint8_t Retries = 0;
    ActionEvent Action{};
    AnimationVariables Variables{};
    // Set when the action's clips are in, as an owner's game waits for them (RunActionAnimationLoadedCallback).
    std::shared_ptr<std::atomic<bool>> ClipsLoaded;
};

// ActionEvent::Type bit: a refused action's graph writes, sent with no event to replay. 38949 and 38953 run 39004's
// writes (TurnStop: iSyncTurnState 1) whether or not the graph took the event (animation-replay-recovery.md).
constexpr uint32_t kVariablesOnly = 0x8;

[[nodiscard]] inline bool VariablesOnly(const ActionEvent& acAction) noexcept
{
    return (acAction.Type & kVariablesOnly) != 0;
}

// An owner's refused action: sent as variables only when it changed them (acAfter: read after 38949).
[[nodiscard]] inline bool SendRefusedVariables(ActionEvent& aAction, const AnimationVariables& acAfter) noexcept
{
    auto& variables = aAction.Variables;
    if (variables.Booleans == acAfter.Booleans && variables.Integers == acAfter.Integers && variables.Floats == acAfter.Floats)
        return false;
    variables.Booleans = acAfter.Booleans;
    variables.Integers = acAfter.Integers;
    variables.Floats = acAfter.Floats;
    aAction.Type |= kVariablesOnly;
    return true;
}

// A copy's last played action: its owner's tick and the copy's graph time then (the deltas its graph updates got, 20123).
struct ActionAnchor
{
    uint64_t OwnerTick = 0;
    uint64_t GraphUs = 0;
    bool Set = false;
};

// Whether a copy's next action is due. After the last one, as far in its graph's time as on its owner's: its clips run
// at the frames' time while its playback steers (0.85-1.1x), and a clip's end must meet the actions its owner had
// around it (animation-replay-recovery.md, "Actions spaced on the copy's graph time"). Never more than kAheadMs before
// playback; due kBehindMs after it, when its graph stood still or nothing anchors it. aAtPlayback: see AtPlayback.
[[nodiscard]] constexpr bool ActionDue(const ActionAnchor& acAnchor, uint64_t aActionTick, uint64_t aGraphUs, uint64_t aPlaybackTick,
                                       bool aAtPlayback = false) noexcept
{
    constexpr uint64_t kAheadMs = 250;
    constexpr uint64_t kBehindMs = 250;
    if (aActionTick + kBehindMs <= aPlaybackTick)
        return true;
    if (aAtPlayback || !acAnchor.Set || aActionTick > aPlaybackTick + kAheadMs)
        return aActionTick <= aPlaybackTick;
    const uint64_t cGapUs = aActionTick > acAnchor.OwnerTick ? (aActionTick - acAnchor.OwnerTick) * 1000 : 0;
    return aGraphUs >= acAnchor.GraphUs && aGraphUs - acAnchor.GraphUs >= cGapUs;
}

// Whether the front of a copy's queue comes at playback, not on its graph's clock: a paired action (settled against
// this game's partner) or a death (its owner's timeline), or an action one of those, due, waits behind. A pair played
// late is refused once its partner died of the forwarded hit; a death played late leaves the copy standing.
template <class Actions> [[nodiscard]] bool AtPlayback(const Actions& acActions, uint64_t aPlaybackTick) noexcept
{
    bool front = true;
    for (const auto& cAction : acActions)
    {
        if ((!cAction.TargetEventName.empty() || DeathActions::IsDying(cAction.State1)) && (front || cAction.Tick <= aPlaybackTick))
            return true;
        front = false;
    }
    return false;
}

// Whether a step in kClips may go on: its clips are in, or they are not coming. A clip started without its file
// lasts 0.01333 s (hkbClipGenerator 59263) and the graph skips it.
[[nodiscard]] inline bool ClipWaitOver(const AnimationReplayStep& acStep, uint32_t aUpdatesWaited) noexcept
{
    constexpr uint32_t cMaxClipWaitUpdates = 120;
    return !acStep.ClipsLoaded || acStep.ClipsLoaded->load(std::memory_order_acquire) || aUpdatesWaited > cMaxClipWaitUpdates;
}

// Called once per graph update: variables, one graph update, then the event (20123); variables only: no event.
template <class Load, class Play>
void RunAnimationReplayStep(AnimationReplayStep& aStep, Load&& aLoad, Play&& aPlay)
{
    using Step = AnimationReplayStep;
    uint8_t phase = aStep.Phase.load(std::memory_order_acquire);
    if (phase != Step::kVariables && phase != Step::kAction)
        return;
    if (!aStep.Phase.compare_exchange_strong(phase, Step::kBusy, std::memory_order_acq_rel))
        return;
    if (phase == Step::kVariables)
    {
        aLoad(aStep.Variables);
        aStep.Phase.store(Step::kAction, std::memory_order_release);
        return;
    }
    constexpr uint8_t cMaxRetries = 3;
    if (VariablesOnly(aStep.Action) || aPlay())
        aStep.Phase.store(Step::kTaken, std::memory_order_release);
    else
        aStep.Phase.store(aStep.Retries++ < cMaxRetries ? Step::kAction : Step::kRefused, std::memory_order_release);
}
