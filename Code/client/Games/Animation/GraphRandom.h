#pragma once

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A copy's graph makes its owner's random picks (hkbBehaviorGraph's generator). An action carries the seed; a pick no
// action started sends a sync point, an action-like entry a copy in step ignores and any other copy loads.
// docs/re/systems/animation-replay-recovery.md.
namespace GraphRandom
{
constexpr uint64_t cInStepMs = 500;    // a copy picking this close to its owner, on its playback time, is in step
constexpr uint64_t cActionPickMs = 500; // a pick this soon after an action is that action's
constexpr uint64_t cPointGapMs = 1000; // at most one sync point an actor this often
constexpr uint32_t cReachDraws = 16;    // a copy this few draws behind its owner may be about to catch up

// An owner's picks.
struct Owner
{
    uint32_t Seed = 0;
    uint64_t ActionAt = 0;
    uint64_t PointAt = 0;
};

// Whether this seed is a pick to send a sync point for.
inline bool Picked(Owner& aOwner, uint32_t aSeed, uint64_t aNow) noexcept
{
    if (!aSeed || aSeed == aOwner.Seed)
        return false;
    const bool cFirst = !aOwner.Seed;
    if (aOwner.ActionAt && aNow < aOwner.ActionAt + cActionPickMs)
    {
        aOwner.Seed = aSeed;
        return false;
    }
    if (aOwner.PointAt && aNow < aOwner.PointAt + cPointGapMs)
        return false;
    aOwner.Seed = aSeed;
    if (cFirst)
        return false;
    aOwner.PointAt = aNow;
    return true;
}

// A copy's picks, on its playback time.
struct Copy
{
    uint32_t Seed = 0;
    uint64_t ChangedAt = 0;
    bool Late = false; // it loaded a point after waiting for its pick: that wait is now its lag
};

inline void Track(Copy& aCopy, uint32_t aSeed, uint64_t aNow) noexcept
{
    if (aSeed != aCopy.Seed)
    {
        aCopy.Seed = aSeed;
        aCopy.ChangedAt = aNow;
    }
}

// Whether a graph on this seed reaches that one in a few draws (59406: seed * 1664525 + 1013904223).
inline bool Reaches(uint32_t aSeed, uint32_t aTarget) noexcept
{
    for (uint32_t i = 0; i < cReachDraws; ++i)
        if ((aSeed = aSeed * 1664525 + 1013904223) == aTarget)
            return true;
    return false;
}

enum class Point
{
    kWait,   // its moment is not over: the copy may yet pick
    kInStep, // the copy made this pick itself
    kLoad,   // it did not: it takes the owner's graph
};

// A clip starts at a pick, so a load at the point's own moment puts the copy on its owner's time: a copy that cannot
// be about to pick loads then. One a draw behind gets a moment; if it came to nothing, the next point does not wait.
inline Point Judge(Copy& aCopy, uint32_t aSeed, uint64_t aPointTick, uint64_t aNow) noexcept
{
    const uint64_t cGap = aCopy.ChangedAt > aPointTick ? aCopy.ChangedAt - aPointTick : aPointTick - aCopy.ChangedAt;
    if (aCopy.Seed == aSeed && cGap <= cInStepMs)
    {
        aCopy.Late = false;
        return Point::kInStep;
    }
    if (aNow < aPointTick)
        return Point::kWait;
    const bool cMayPick = !aCopy.Late && Reaches(aCopy.Seed, aSeed);
    if (cMayPick && aNow < aPointTick + cInStepMs)
        return Point::kWait;
    aCopy.Late = cMayPick;
    return Point::kLoad;
}

// A BSEventEveryNEventsModifier's count: a graph snapshot leaves these out.
struct Counter
{
    std::string Name;
    int32_t Seen = 0;
    int32_t Needed = 0;
};

inline std::string Pack(const std::vector<Counter>& acCounters)
{
    std::string packed;
    for (const auto& cCounter : acCounters)
        packed += cCounter.Name + ':' + std::to_string(cCounter.Seen) + ':' + std::to_string(cCounter.Needed) + ';';
    return packed;
}

inline std::vector<Counter> Unpack(std::string_view aPacked)
{
    std::vector<Counter> counters;
    while (!aPacked.empty())
    {
        const auto cEnd = aPacked.find(';');
        const auto cEntry = aPacked.substr(0, cEnd);
        const auto cSeen = cEntry.rfind(':', cEntry.rfind(':') - 1);
        const auto cNeeded = cEntry.rfind(':');
        if (cEnd == std::string_view::npos || cSeen == std::string_view::npos || cNeeded == std::string_view::npos)
            break;
        Counter counter{std::string(cEntry.substr(0, cSeen))};
        std::from_chars(cEntry.data() + cSeen + 1, cEntry.data() + cNeeded, counter.Seen);
        std::from_chars(cEntry.data() + cNeeded + 1, cEntry.data() + cEntry.size(), counter.Needed);
        counters.push_back(std::move(counter));
        aPacked.remove_prefix(cEnd + 1);
    }
    return counters;
}
} // namespace GraphRandom
