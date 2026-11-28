#pragma once

#include <Structs/ScriptValue.h>

#include <cstdint>

// On the host, a guest's character sees what the guest's game says it sees: the engine answers a player's line of sight
// from that game's camera only (docs/re/systems/papyrus-guest-scripts.md, "A player's line of sight").
namespace GuestSight
{
// The guest's game answered one of the host's sight questions. False: aReplyId is not one (a waiting stack's).
bool Answer(uint32_t aReplyId, const LocalValue& acResult) noexcept;
} // namespace GuestSight
