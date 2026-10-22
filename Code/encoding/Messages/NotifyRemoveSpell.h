#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyRemoveSpell final : NotifyMessage, Payloads::RemoveSpell
{
    static constexpr NotifyOpcode Opcode = kNotifyRemoveSpell;
    using Payload = Payloads::RemoveSpell;

    NotifyRemoveSpell()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyRemoveSpell& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
