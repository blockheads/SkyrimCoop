#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestEquipmentSnapshot final : RequestMessage, Payloads::EquipmentSnapshot
{
    static constexpr RequestOpcode Opcode = kRequestEquipmentSnapshot;
    using Payload = Payloads::EquipmentSnapshot;

    RequestEquipmentSnapshot()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestEquipmentSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
