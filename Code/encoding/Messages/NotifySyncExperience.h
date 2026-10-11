#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifySyncExperience final : ServerMessage, Payloads::SyncExperience
{
    static constexpr ServerOpcode Opcode = kNotifySyncExperience;
    using Payload = Payloads::SyncExperience;

    NotifySyncExperience()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifySyncExperience& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
