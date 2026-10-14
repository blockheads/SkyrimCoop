#pragma once

#include "Message.h"

// The slowest clock the other players' games want: the receiving game runs no faster.
struct NotifyTimeScale final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyTimeScale;

    NotifyTimeScale()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyTimeScale& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Multiplier == acRhs.Multiplier; }

    float Multiplier{1.f};
};
