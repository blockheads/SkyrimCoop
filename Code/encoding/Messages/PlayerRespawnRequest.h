#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct PlayerRespawnRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kPlayerRespawnRequest;

    PlayerRespawnRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerRespawnRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode(); }
};
