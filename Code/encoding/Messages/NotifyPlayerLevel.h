#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct NotifyPlayerLevel final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPlayerLevel;

    NotifyPlayerLevel()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerLevel& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && NewLevel == acRhs.NewLevel; }

    uint32_t PlayerId{};
    uint16_t NewLevel{};
};
