#pragma once

#include <Structs/ScriptValue.h>

// On a guest: its own script changed the world (a quest's stage), skipped here (PlayerNatives::SendToHost): the host's
// game makes the call, for this game's player (docs/design/world-state-sync.md, "Only the host's game decides").
struct WorldCallEvent
{
    LocalCall Call{};
};
