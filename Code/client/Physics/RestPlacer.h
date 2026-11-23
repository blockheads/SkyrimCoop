#pragma once

#include <cstdint>
#include <optional>

// Puts another game's rest on a body this game simulates, a step an update (docs/design/physics-sync.md, "RestPlacer's
// steps"). No engine: ClientTests.
struct RestPlacer
{
    static constexpr uint64_t kGlideMs = 500;
    static constexpr uint64_t kSleepMs = 2000;

    enum class Step
    {
        kGlide,
        kPlace,
        kSleep,
        kHold,
        kLeave,
        kKeep,
    };

    // aHolds: a reference's (kHold when it left in the place's step); a ragdoll's joints settle it where it lands.
    explicit RestPlacer(uint64_t aTick = 0, bool aHolds = true) noexcept
        : Since(aTick)
        , Holds(aHolds)
    {
    }

    // aNear: every body at its rest; aFar: one past BodyTolerances::kPlaceFar (put there, no glide); aStill: every
    // body still (BodyPrediction::StillToSleep); aAsleep: asleep there; aWoken: done, then a body awake off it.
    [[nodiscard]] Step Next(bool aNear, bool aFar, bool aStill, bool aAsleep, bool aWoken, uint64_t aTick) noexcept
    {
        if (Done)
        {
            Keeping = !aAsleep && (Keeping || aWoken);
            if (!Keeping)
                return Step::kLeave;
            // Steered until it is back and still, then put to sleep again.
            return aNear && aStill ? Step::kSleep : Step::kKeep;
        }
        // Held there until read asleep at it (its island's sleep is refused while another body in it moves).
        if (Holding)
        {
            Done = (aNear && aAsleep) || aTick - *Holding >= kSleepMs;
            return Done ? Step::kLeave : Step::kHold;
        }
        // There already, asleep: its sleep is a no-op (an inactive island: DeactivateIsland), which completes it.
        if (aNear && aAsleep && !Placed)
            return Step::kSleep;
        if (Placed)
            return aNear || !Holds ? Step::kSleep : Step::kHold;
        // A reference near is put exactly; a ragdoll near and still sleeps where its joints hold it.
        if (!Holds && aNear && aStill)
            return Step::kSleep;
        if (aTick - Since < kGlideMs && !aFar && !(Holds && aNear))
            return Step::kGlide;
        Placed = true;
        return Step::kPlace;
    }

    // kSleep's result: done once asleep, or after kSleepMs of trying (an island another body keeps awake).
    void Slept(bool aAsleep, uint64_t aTick) noexcept
    {
        if (!Sleeping)
            Sleeping = aTick;
        Done = aAsleep || aTick - *Sleeping >= kSleepMs;
    }
    // From here held there (kHold) until asleep at it, kSleepMs at most.
    void Hold(uint64_t aTick) noexcept
    {
        if (!Done && !Holding)
            Holding = aTick;
    }

    uint64_t Since{};
    bool Holds{};
    std::optional<uint64_t> Sleeping;
    std::optional<uint64_t> Holding;
    bool Placed{};
    bool Done{};
    bool Keeping{}; // done, then woken off it: steered back until it sleeps there
};
