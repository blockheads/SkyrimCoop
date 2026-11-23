#pragma once

#include <cstdint>

// Every distance and time physics sync judges bodies by, each named once (docs/design/physics-authority-plan.md).
namespace BodyTolerances
{
// A body moving on the ground goes on the stream this often: every game shows it between two states its authority
// had (BodyPlayback), 100 ms behind, never a guess past them.
constexpr uint64_t kMovingMs = 45;
constexpr float kMovedDistance = 0.05f; // it moved since the last state sent
constexpr float kMovedAngle = 0.002f;
constexpr float kMovedSpeed = 1.f;      // units/s: or its speed changed (its stop)
constexpr float kKnockSpeed = 30.f;     // units/s: a change this sudden (a knock, a bounce) goes at once
// A free fall is an arc every game guesses alike: sent when the guess is this far off.
constexpr float kFallDistance = 0.5f;
constexpr float kFallAngle = 0.05f;
// Two games' results this far apart disagree (a rest's, a placement's).
constexpr float kSendDistance = 2.f;  // units
constexpr float kSendAngle = 0.05f;   // radians
constexpr uint64_t kRefreshMs = 500;  // an awake body's state goes at least this often (a lost one's repair)
// Shown past its newest state (the next one lost) at most this long.
constexpr uint64_t kCoastMs = 100;
constexpr uint64_t kRestRefreshMs = 5000; // a resting body's comes round this often
constexpr uint8_t kChangedRepeats = 3;    // a changed claim or rest goes in this many updates running

// A body this near a pose lies there (a settle in place, not a move).
constexpr float kSameDistance = 0.5f;
constexpr float kSameAngle = 0.005f;

// Steered onto a state up to this far off; further, put there.
constexpr float kPlaceFar = 64.f;

// A body keeping one pose this long is still, whatever Havok's island says (Fiedler forces a rest so too).
constexpr float kStillDistance = 0.5f;
constexpr float kStillAngle = 0.01f;
constexpr uint64_t kStillMs = 300;

// A player's touch keeps its pile from resting this long (a capsule leaning on it: 62648), at most kRestWaitMs.
constexpr uint64_t kLeanMs = 500;
constexpr uint64_t kRestWaitMs = 5000;

// An authority silent this long gives its bodies back.
constexpr uint64_t kSilentMs = 2000;
} // namespace BodyTolerances
