#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

struct RequestSetWaypoint final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestSetWaypoint;

    RequestSetWaypoint() : RequestMessage(Opcode)
    {
    }

    virtual ~RequestSetWaypoint() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestSetWaypoint& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Position == acRhs.Position && WorldSpaceFormID == acRhs.WorldSpaceFormID;
    }

    Vector3_NetQuantize Position;
    GameId WorldSpaceFormID;
};
