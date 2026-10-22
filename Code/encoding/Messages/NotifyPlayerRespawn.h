#pragma once

#include "Message.h"

struct NotifyPlayerRespawn final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPlayerRespawn;

    NotifyPlayerRespawn()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerRespawn& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && GoldLost == acRhs.GoldLost; }

    int32_t GoldLost{};
};
