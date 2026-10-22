#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct NotifyPlayerJoined final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPlayerJoined;

    NotifyPlayerJoined()
        : NotifyMessage(Opcode)
    {
    }

    virtual ~NotifyPlayerJoined() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerJoined& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Username == acRhs.Username && Level == acRhs.Level && IsHost == acRhs.IsHost; }

    uint32_t PlayerId{};
    std::string Username{};
    uint16_t Level{};
    // The session's host (the authority, who alone may kick).
    bool IsHost{};
};
