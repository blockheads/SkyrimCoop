#pragma once

#include <Messages/Payloads.h>

#include <cstdint>

// A trap touched this game's player, a guest's: the host's script decides. The payload's ids are not filled.
struct TrapContactEvent
{
    uint32_t TrapId{};
    uint32_t TargetId{};
    Payloads::TrapContact Contact{};
};

// The host's script hit a guest's copy with a trap: its game takes it. The payload's ids are not filled.
struct TrapHitEvent
{
    uint32_t TrapId{};
    uint32_t TargetId{};
    Payloads::TrapHit Hit{};
};
