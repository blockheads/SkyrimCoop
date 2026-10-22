#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct NotifyRemoveWaypoint final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyRemoveWaypoint;

    NotifyRemoveWaypoint() : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRemoveWaypoint& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode();
    }
};
