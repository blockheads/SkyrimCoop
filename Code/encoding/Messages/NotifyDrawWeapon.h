#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyDrawWeapon final : NotifyMessage, Payloads::DrawWeapon
{
    static constexpr NotifyOpcode Opcode = kNotifyDrawWeapon;
    using Payload = Payloads::DrawWeapon;

    NotifyDrawWeapon()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyDrawWeapon& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
