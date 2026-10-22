#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct ProjectileLaunchRequest final : RequestMessage, Payloads::ProjectileLaunch
{
    static constexpr RequestOpcode Opcode = kProjectileLaunchRequest;
    using Payload = Payloads::ProjectileLaunch;

    ProjectileLaunchRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const ProjectileLaunchRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
