#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct AddTargetRequest final : RequestMessage, Payloads::AddTarget
{
    static constexpr RequestOpcode Opcode = kAddTargetRequest;
    using Payload = Payloads::AddTarget;

    AddTargetRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const AddTargetRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
