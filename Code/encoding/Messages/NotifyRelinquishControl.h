#pragma once

#include "Message.h"

struct NotifyRelinquishControl final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyRelinquishControl;

    NotifyRelinquishControl()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRelinquishControl& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && SessionId == acRhs.SessionId; }

    uint32_t SessionId;
};
