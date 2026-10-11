#pragma once

#include "Message.h"
#include "Payloads.h"

struct NewPackageRequest final : ClientMessage, Payloads::NewPackage
{
    static constexpr ClientOpcode Opcode = kNewPackageRequest;
    using Payload = Payloads::NewPackage;

    NewPackageRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NewPackageRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
