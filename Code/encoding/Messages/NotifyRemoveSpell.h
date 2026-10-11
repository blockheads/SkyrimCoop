#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyRemoveSpell final : ServerMessage, Payloads::RemoveSpell
{
    static constexpr ServerOpcode Opcode = kNotifyRemoveSpell;
    using Payload = Payloads::RemoveSpell;

    NotifyRemoveSpell()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyRemoveSpell& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
