#pragma once

#include <Structs/ScriptValue.h>

// A latent party call this game's script made (a story's Message.Show) returned: its copies on the other games end
// with the same result (PartyNatives, docs/design/party-calls.md, "A box's answer").
struct CallEndedEvent
{
    uint32_t Key{}; // its stack here, the key its copies go by
    LocalValue Result{};
};
