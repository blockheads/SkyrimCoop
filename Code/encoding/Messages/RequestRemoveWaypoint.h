#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct RequestRemoveWaypoint final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestRemoveWaypoint;

    RequestRemoveWaypoint() : RequestMessage(Opcode)
    {
    }

    virtual ~RequestRemoveWaypoint() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestRemoveWaypoint& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode();
    }
};
