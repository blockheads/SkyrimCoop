#pragma once

#include "Message.h"
#include "Payloads.h"

struct ProjectileLaunchRequest final : RequestMessage, Payloads::ProjectileLaunch
{
    static constexpr RequestOpcode Opcode = kProjectileLaunchRequest;
    using Payload = Payloads::ProjectileLaunch;

    ProjectileLaunchRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const ProjectileLaunchRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
