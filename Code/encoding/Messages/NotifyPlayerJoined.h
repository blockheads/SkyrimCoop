#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct NotifyPlayerJoined final : MessageOf<NotifyPlayerJoined, kNotifyPlayerJoined>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(PlayerId, Username, Level, IsHost, Arrived);
    }
    bool operator==(const NotifyPlayerJoined&) const noexcept = default;

    uint32_t PlayerId{};
    std::string Username{};
    uint16_t Level{};
    // The session's host (the authority, who alone may kick).
    bool IsHost{};
    // Joining now, not one already in the session (each is announced to a newcomer).
    bool Arrived{};
};
