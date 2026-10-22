#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct NotifyEquipmentSnapshot final : NotifyMessage, Payloads::EquipmentSnapshot
{
    static constexpr NotifyOpcode Opcode = kNotifyEquipmentSnapshot;
    using Payload = Payloads::EquipmentSnapshot;

    NotifyEquipmentSnapshot()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyEquipmentSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
