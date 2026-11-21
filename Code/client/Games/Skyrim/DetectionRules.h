#pragma once

#include <cstdint>

// HookUpdateDetectionState's rule with no engine (docs/re/systems/stealth.md). Checked by ClientTests; the hook and
// GameTests' fake engine both follow it.
namespace DetectionRules
{
constexpr int32_t kUnseen = -1000; // a pass with detection off (DoDetectionJob 42742)

// What a detection pass leaves on a witness's knowledge of a target (DetectionState 42760: level, LOS, seen).
struct Pass
{
    int32_t Level = kUnseen;
    bool LineOfSight = false; // +0x14
    bool Seen = false;        // +0x15, counted by the HUD's highest detection (41408)
    uint8_t Unk16 = 0;        // +0x16
    bool operator==(const Pass&) const = default;
};

// The pass a witness's knowledge gets, always installed: a copy (another player's, or an NPC another game owns)
// judging this game's player sees nothing: that player's eye is its owner's NPCs' (piece 8). Skipping the pass would
// leave its request queued, and 42702 then answers an older level at any age.
[[nodiscard]] constexpr Pass Installed(bool aWitnessIsCopy, bool aTargetIsLocalPlayer, Pass aComputed) noexcept
{
    return aWitnessIsCopy && aTargetIsLocalPlayer ? Pass{} : aComputed;
}

// What the host's NPCs see of a guest's player (NotifyStealthEye): its copy's highest level (41408), how many see it,
// its lowest stealth points in their fights (46877). On that guest the eye's reads of its player answer it.
struct EyeReading
{
    int32_t Level = kUnseen;
    uint32_t Seeing = 0;
    float Stealth = 100.f; // 46877 with no fight: fDefault
    bool operator==(const EyeReading&) const = default;
};

// The HUD's eye (40593): in combat 100 minus the stealth points, else 100 for any level above 0; never below 0.
[[nodiscard]] constexpr int32_t EyeShows(bool aInCombat, int32_t aLevel, float aStealth) noexcept
{
    const int32_t cShown = aInCombat ? 100 - static_cast<int32_t>(aStealth) : (aLevel > 0 ? 100 : aLevel);
    return cShown > 0 ? cShown : 0;
}

// The host sends a reading again when the eye or a caller could read it otherwise: seen or not, someone seeing or
// not, the level by kLevelStep (the stolen item owner's check adds it to a modifier, 41382), the stealth points by a
// whole point (40593 shows them as an int).
constexpr int32_t kLevelStep = 5;
[[nodiscard]] constexpr bool EyeChanged(const EyeReading& acSent, const EyeReading& acNow) noexcept
{
    const int32_t cLevelStep = acSent.Level > acNow.Level ? acSent.Level - acNow.Level : acNow.Level - acSent.Level;
    return (acSent.Level > 0) != (acNow.Level > 0) || (acSent.Seeing > 0) != (acNow.Seeing > 0) || cLevelStep >= kLevelStep ||
           static_cast<int32_t>(acSent.Stealth) != static_cast<int32_t>(acNow.Stealth);
}

// ProcessTracking (38009) drops a default look at this game's player its tracker does not detect (37758: a state of
// level 0 or less). A copy's look is its owner's, judged on its owner's game: its own detection is not asked.
[[nodiscard]] constexpr bool HeadTrackAsksDetection(bool aCopy) noexcept
{
    return !aCopy;
}
} // namespace DetectionRules
