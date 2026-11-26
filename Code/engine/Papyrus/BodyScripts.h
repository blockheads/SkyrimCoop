#pragma once

#include <cstdint>

// A player's body's scripts (docs/design/dragonborn.md, "The party's calls"): what they change is that player's own,
// never the party's. An effect's, its equipment's, a script on the player, or a quest its body started (a beast form's
// PlayerWerewolfQuest).
namespace BodyScripts
{
void SetInSession(bool aInSession) noexcept;
// PlayerNatives' hook on a quest's start (25003): aQuest started, by the script aScript (a VM handle, 0 for none).
void QuestStarted(uint32_t aQuest, uint64_t aScript) noexcept;
// Whether the script aScript (a VM handle, 0 for none) is a body's.
[[nodiscard]] bool IsBody(uint64_t aScript) noexcept;
} // namespace BodyScripts
