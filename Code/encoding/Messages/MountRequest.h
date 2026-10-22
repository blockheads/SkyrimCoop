#pragma once

#include "Message.h"
#include "Payloads.h"

struct MountRequest final : RequestMessage, Payloads::Mount
{
    static constexpr RequestOpcode Opcode = kMountRequest;
    using Payload = Payloads::Mount;

    MountRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const MountRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
