#pragma once

#include <Physics/BodyPrediction.h>

#include <cstdint>
#include <optional>

// A copy's ragdoll at its owner's rest, all its bodies as one (their joints keep them one island, and 60850 sleeps an
// island; docs/design/physics-sync.md, "A copy's ragdoll rests as a pile"). Steered on, a body never sleeps (78089/78090
// wake it): glided there for half a second at most, put there in one step, put to sleep, then left alone. No engine.
struct RagdollRest
{
    // Every body this near its rest is there: copies reached it within 0.2 to 0.8 units (duo, 2026-11-12).
    static constexpr float kNear = 1.f;          // units
    static constexpr float kNearAngle = 0.05f;   // radians
    // Woken off its rest after it slept (a body knocked into it) it is taken there again, this many times a rest.
    static constexpr uint8_t kAgainAtMost = 2;

    enum class Step
    {
        kGlide, // steered toward the rest (BodyPrediction::Steer over kRestGlideStep)
        kPlace, // every body put at its rest in this step, awake and still
        kSleep, // the island put to sleep (SleepBody), then Slept
        kLeave, // at rest: untouched
    };

    // A new rest (its poses changed) at aNow, in seconds.
    void Start(float aNow) noexcept
    {
        *this = {.Since = aNow};
    }

    // Each update while the owner's rest holds. aNear, aStill: every body (BodyPrediction::StillToSleep); aWoken: one
    // body awake and off its rest.
    [[nodiscard]] Step Next(bool aNear, bool aStill, bool aWoken, float aNow) noexcept
    {
        if (Done)
        {
            if (!aWoken || Again >= kAgainAtMost)
                return Step::kLeave;
            const uint8_t cAgain = Again + 1;
            Start(aNow);
            Again = cAgain;
        }
        if (Placed || (aNear && aStill))
            return Step::kSleep;
        if (aNow - Since < BodyPrediction::kRestGlideSeconds)
            return Step::kGlide;
        Placed = true;
        return Step::kPlace;
    }

    // kSleep's result: done once asleep, or after kRestSleepSeconds of trying (an island with another body moving).
    void Slept(bool aAsleep, float aNow) noexcept
    {
        if (!Sleeping)
            Sleeping = aNow;
        Done = BodyPrediction::RestSlept(aAsleep, aNow - *Sleeping);
    }

    float Since{};
    std::optional<float> Sleeping;
    bool Placed{};
    bool Done{};
    uint8_t Again{};
};
