#pragma once

#include <Structs/ScriptValue.h>

// A script's call PartyNatives::Scope sent away (docs/design/party-calls.md): to the host's game, to the game of the
// guest whose character it ran for, or to every other game.
struct RoutedCallEvent
{
    CallTo To{};
    uint32_t GuestFormId{}; // kOwner: the guest's character
    uint32_t ReplyId{}; // kOwner, one returning a value: the script waiting on it (PlayerNatives::Answer); kOthers, a
                        // latent call: its stack here, its key (CallEndedEvent); 0: none
    LocalCall Call{};
};
