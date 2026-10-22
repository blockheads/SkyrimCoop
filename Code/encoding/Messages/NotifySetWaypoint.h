#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

struct NotifySetWaypoint final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifySetWaypoint;

    NotifySetWaypoint() : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifySetWaypoint& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Position == acRhs.Position && WorldSpaceFormID == acRhs.WorldSpaceFormID;
    }

    Vector3_NetQuantize Position;
    GameId WorldSpaceFormID;
};
