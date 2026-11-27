#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct PlayerLevelRequest final : MessageOf<PlayerLevelRequest, kPlayerLevelRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NewLevel); }
    bool operator==(const PlayerLevelRequest&) const noexcept = default;

    uint16_t NewLevel{};
};
