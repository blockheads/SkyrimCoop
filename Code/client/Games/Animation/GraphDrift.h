#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Does a copy's graph land where its owner's did? After each taken replay, the copy's active states against the
// action's GraphState; a lasting difference loads that snapshot (docs/re/systems/animation-replay-recovery.md, "Drift detector").
namespace GraphDrift
{
// "project/machine=state", sorted: the active states section of SaveState 63601 (hkbCharacter 65148).
using States = std::vector<std::string>;

// Longest owner/copy difference measured was 1.08 s: a mismatch open past 2 s counts as lasting.
constexpr uint64_t cLastingMs = 2000;
// While a mismatch is open the copy is read again this often (the measurement's 10/s).
constexpr uint64_t cSampleMs = 100;

// The active state names of TESObjectREFR::SaveAnimationGraphState's bytes; nullopt if they do not parse.
[[nodiscard]] std::optional<States> ActiveStates(std::string_view acSaved) noexcept;

struct Tracker
{
    States Expected;      // the owner's snapshot the open mismatch is measured against
    std::string Snapshot; // its saved graph, loaded if the mismatch lasts
    States Actual;    // the copy's states when it opened
    uint64_t Since = 0;
    uint64_t LastSample = 0;
    uint32_t ActionId = 0;
    std::string Event;
    bool Open = false;
    bool Lasting = false;
};

// Paired actions, missing snapshots and players' copies (first-person graphs) are not compared: read the copy only if this holds.
[[nodiscard]] inline bool Compares(bool aPaired, bool aPlayer, bool aHasSnapshot) noexcept
{
    return !aPaired && !aPlayer && aHasSnapshot;
}
// A taken replay: a fresh comparison; an open one ends, superseded.
void Taken(Tracker& aTracker, uint32_t aFormId, uint32_t aActionId, std::string_view acEvent, bool aPaired, bool aPlayer,
           std::string_view acSnapshot, const std::optional<States>& acExpected, const std::optional<States>& acActual, uint64_t aNow) noexcept;
// Whether an open mismatch wants a fresh read of the copy now.
[[nodiscard]] bool SampleDue(const Tracker& acTracker, uint64_t aNow) noexcept;
// A fresh read of the copy: matched ends it, unreadable drops it, apart past cLastingMs counts it lasting once and
// returns true: load Tracker::Snapshot into the copy now, then report it with Loaded.
[[nodiscard]] bool Sample(Tracker& aTracker, uint32_t aFormId, const std::optional<States>& acActual, uint64_t aNow) noexcept;
// A sync point (GraphRandom.h) is the owner's newer snapshot: an open mismatch measures against it from now on.
void Retarget(Tracker& aTracker, std::string_view acSnapshot, const std::optional<States>& acExpected) noexcept;
// The snapshot's load: a loaded copy is read again at its next update, which should end the mismatch as matched.
void Loaded(Tracker& aTracker, uint32_t aFormId, bool aLoaded) noexcept;
} // namespace GraphDrift
