#pragma once

#include "Message.h"

// The slowest clock the other players' games want: the receiving game runs no faster.
struct NotifyTimeScale final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyTimeScale;

    NotifyTimeScale()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyTimeScale& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Multiplier == acRhs.Multiplier; }

    float Multiplier{1.f};
};
