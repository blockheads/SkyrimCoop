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

// The pass a witness's knowledge gets, always installed: another player's copy judging this game's player sees
// nothing. Skipping the pass would leave its request queued, and 42702 then answers an older level at any age.
[[nodiscard]] constexpr Pass Installed(bool aWitnessIsOtherPlayer, bool aTargetIsLocalPlayer, Pass aComputed) noexcept
{
    return aWitnessIsOtherPlayer && aTargetIsLocalPlayer ? Pass{} : aComputed;
}
} // namespace DetectionRules
