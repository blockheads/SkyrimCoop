#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifySpellCast final : ServerMessage, Payloads::SpellCast
{
    static constexpr ServerOpcode Opcode = kNotifySpellCast;
    using Payload = Payloads::SpellCast;

    NotifySpellCast()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifySpellCast& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
