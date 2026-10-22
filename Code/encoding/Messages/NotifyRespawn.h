#pragma once

#include "Message.h"

struct NotifyRespawn final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyRespawn;
    NotifyRespawn()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRespawn& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId; }

    uint32_t ActorId;
};
