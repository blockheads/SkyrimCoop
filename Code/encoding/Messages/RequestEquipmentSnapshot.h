#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestEquipmentSnapshot final : ClientMessage, Payloads::EquipmentSnapshot
{
    static constexpr ClientOpcode Opcode = kRequestEquipmentSnapshot;
    using Payload = Payloads::EquipmentSnapshot;

    RequestEquipmentSnapshot()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestEquipmentSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
