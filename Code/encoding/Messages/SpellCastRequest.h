#pragma once

#include "Message.h"
#include "Payloads.h"

struct SpellCastRequest final : ClientMessage, Payloads::SpellCast
{
    static constexpr ClientOpcode Opcode = kSpellCastRequest;
    using Payload = Payloads::SpellCast;

    SpellCastRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const SpellCastRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
