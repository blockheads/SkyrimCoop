#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct NotifyPlayerLevel final : MessageOf<NotifyPlayerLevel, kNotifyPlayerLevel>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(PlayerId, NewLevel); }
    bool operator==(const NotifyPlayerLevel&) const noexcept = default;

    uint32_t PlayerId{};
    uint16_t NewLevel{};
};
