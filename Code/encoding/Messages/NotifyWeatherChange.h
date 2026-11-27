#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct NotifyWeatherChange final : MessageOf<NotifyWeatherChange, kNotifyWeatherChange>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id); }
    bool operator==(const NotifyWeatherChange&) const noexcept = default;

    GameId Id{};
};
