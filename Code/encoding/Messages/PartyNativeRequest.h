#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct PartyNativeRequest final : RequestMessage, Payloads::PartyNative
{
    static constexpr RequestOpcode Opcode = kPartyNativeRequest;
    using Payload = Payloads::PartyNative;

    PartyNativeRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const PartyNativeRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
