#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>
#include <Wire/Serialization.h>

// Another game's script's party call; Source is that game's player (its PlayerId), whose cutscene it is.
struct NotifyPartyNative final : NotifyMessage, Payloads::PartyNative
{
    static constexpr NotifyOpcode Opcode = kNotifyPartyNative;
    using Payload = Payloads::PartyNative;

    NotifyPartyNative()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        Payload::SerializePayload(aWriter);
        Serialization::WriteVarInt(aWriter, Source);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        Payload::DeserializePayload(aReader);
        Source = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    }

    bool operator==(const NotifyPartyNative& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs) && Source == acRhs.Source;
    }

    uint32_t Source{};
};
