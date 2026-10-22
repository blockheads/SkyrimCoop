#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct PlayerLevelRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kPlayerLevelRequest;

    PlayerLevelRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerLevelRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && NewLevel == acRhs.NewLevel; }

    uint16_t NewLevel{};
};
