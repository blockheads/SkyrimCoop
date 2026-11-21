#pragma once

#include <Games/Skyrim/DetectionRules.h>

#include <optional>

struct Actor;

// The HUD's eye's reads (docs/re/systems/stealth.md, "The eye is the host's"): the highest detection level of an actor
// (ProcessLists 41408) and its lowest stealth points in a fight (CombatManager 46877).
namespace StealthEye
{
// What this game's NPCs see of the actor now, asking as the HUD asks of the player (41408 queues stale states).
[[nodiscard]] DetectionRules::EyeReading Read(Actor& aTarget) noexcept;
// On a guest: both reads of this game's player answer the host's reading (none: this game's own again).
void Hold(const std::optional<DetectionRules::EyeReading>& acReading) noexcept;
} // namespace StealthEye
