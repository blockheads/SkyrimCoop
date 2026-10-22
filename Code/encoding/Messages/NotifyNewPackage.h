#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyNewPackage final : NotifyMessage, Payloads::NewPackage
{
    static constexpr NotifyOpcode Opcode = kNotifyNewPackage;
    using Payload = Payloads::NewPackage;

    NotifyNewPackage()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyNewPackage& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
