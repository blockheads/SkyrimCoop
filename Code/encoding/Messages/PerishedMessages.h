#pragma once

// A player going down (in a session a player is essential: it bleeds out, then respawns), for every game's chat.
#include "Message.h"
#include <Wire/Buffer.h>

// This game's player went down.
struct PlayerPerishedRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kPlayerPerishedRequest;

    PlayerPerishedRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerPerishedRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode(); }
};

// A player went down: to every game, the player's own too.
struct NotifyPlayerPerished final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPlayerPerished;

    NotifyPlayerPerished()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerPerished& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Username == acRhs.Username;
    }

    uint32_t PlayerId{};
    std::string Username;
};
