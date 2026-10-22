#pragma once

#include "Message.h"

struct NotifyRespawn final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyRespawn;
    NotifyRespawn()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRespawn& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId; }

    uint32_t ActorId;
};
