#pragma once

#include "Message.h"
#include "Payloads.h"

struct SpellCastRequest final : RequestMessage, Payloads::SpellCast
{
    static constexpr RequestOpcode Opcode = kSpellCastRequest;
    using Payload = Payloads::SpellCast;

    SpellCastRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const SpellCastRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
