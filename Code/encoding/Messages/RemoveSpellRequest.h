#pragma once

#include "Message.h"
#include "Payloads.h"

struct RemoveSpellRequest final : RequestMessage, Payloads::RemoveSpell
{
    static constexpr RequestOpcode Opcode = kRequestRemoveSpell;
    using Payload = Payloads::RemoveSpell;

    RemoveSpellRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RemoveSpellRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
