#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct DrawWeaponRequest final : RequestMessage, Payloads::DrawWeapon
{
    static constexpr RequestOpcode Opcode = kDrawWeaponRequest;
    using Payload = Payloads::DrawWeapon;

    DrawWeaponRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const DrawWeaponRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
