#pragma once

#include <cstdint>
#include <optional>

// Putting another game's rest on a body this game simulates (docs/re/systems/physics.md, "A rest needs its contacts",
// "A pile sleeps together"), one for a reference's body and a copy's ragdoll alike. No engine: ClientTests.
//
//   kGlide  steered onto it, kGlideMs at most (put there at once it snapped up to 16 units)
//   kPlace  put there awake and still: the next physics step finds its contacts (61518, 61321)
//   kSleep  its island put to sleep once all of it is still (60850 sleeps the island), kSleepMs at most
//   kHold   a reference that left in that step does not stay there by itself: held there asleep
//   kLeave  there
//   kKeep   done, then awake off it (nothing holds it up here): steered back each
//           update, never put (glided and put again it jumped 3.7 units a second, then was left where it fell)
//
// Each update calls Next once, after a physics step: a place is never slept in the same update.
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
    // kHold's result: held there asleep.
    void Held() noexcept { Done = true; }

    uint64_t Since{};
    bool Holds{};
    std::optional<uint64_t> Sleeping;
    bool Placed{};
    bool Done{};
    bool Keeping{}; // done, then woken off it: steered back until it sleeps there
};
