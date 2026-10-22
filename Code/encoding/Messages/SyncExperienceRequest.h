#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct SyncExperienceRequest final : RequestMessage, Payloads::SyncExperience
{
    static constexpr RequestOpcode Opcode = kSyncExperienceRequest;
    using Payload = Payloads::SyncExperience;

    SyncExperienceRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const SyncExperienceRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
