#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct NotifyWeatherChange final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyWeatherChange;

    NotifyWeatherChange()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyWeatherChange& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id; }

    GameId Id{};
};
