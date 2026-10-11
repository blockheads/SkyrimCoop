#pragma once

#include "Message.h"
#include "Payloads.h"

struct RemoveSpellRequest final : ClientMessage, Payloads::RemoveSpell
{
    static constexpr ClientOpcode Opcode = kRequestRemoveSpell;
    using Payload = Payloads::RemoveSpell;

    RemoveSpellRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RemoveSpellRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
