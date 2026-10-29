#pragma once

#include "Trail.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Diagnostics
{
// Which way a message went, seen from this game.
enum class MessageWay : char
{
    Sent = 'S',     // a request this game sent to the host's rules
    Received = 'R', // a notification this game's services got
    HostIn = 'I',   // the host's rules got a request from a game (the host's own included)
    HostOut = 'O',  // the host's rules sent a notification to a game
};

struct MessageEntry
{
    int64_t TimeMs;       // since the Unix epoch
    uint64_t SessionTick; // TransportService::GetTick: the host's clock in every game
    MessageWay Way;
    const char* Name; // the opcode's name (Opcodes.h), static
    uint32_t Peer;    // the host's rules: the game's connection; a game: 0
    uint32_t Size;    // bytes on the wire
    uint64_t Seq{};   // its number in the trail
};

// The newest messages, for a bug report's messages.txt.
using MessageTrail = Trail<MessageEntry>;

// One line per entry: "<unix ms> t=<tick> <way> <name> peer=<peer> <size>B".
[[nodiscard]] std::string FormatMessages(const std::vector<MessageEntry>& acEntries);

// This game's, the last 5 minutes.
MessageTrail& Messages() noexcept;
// Records one now (the wall clock's time).
void RecordMessage(uint64_t aSessionTick, MessageWay aWay, const char* acpName, uint32_t aPeer, uint32_t aSize) noexcept;
} // namespace Diagnostics
