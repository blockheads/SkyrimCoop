#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

// The party tether: a player travelled by itself (EnterCellRequest::Travel::kOwn) and everyone else goes where it
// arrived. A game already there stays.
struct NotifyPartyTravel final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyTravel;

    NotifyPartyTravel() : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyTravel& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId &&
               Position == acRhs.Position;
    }

    GameId CellId{};
    GameId WorldSpaceId{}; // empty: an interior
    Vector3_NetQuantize Position{};
};
