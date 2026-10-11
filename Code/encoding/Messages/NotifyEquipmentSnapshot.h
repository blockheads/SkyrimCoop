#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyEquipmentSnapshot final : ServerMessage, Payloads::EquipmentSnapshot
{
    static constexpr ServerOpcode Opcode = kNotifyEquipmentSnapshot;
    using Payload = Payloads::EquipmentSnapshot;

    NotifyEquipmentSnapshot()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyEquipmentSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
