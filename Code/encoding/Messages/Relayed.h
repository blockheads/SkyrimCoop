#pragma once

#include "Message.h"
#include "Payloads.h"
#include "RelayedMessages.h"
#include <Wire/Archive.h>
#include <Wire/Buffer.h>

#include <type_traits>

// A request or notification carrying a shared payload unchanged: its bytes are the payload's (its Fields, or its
// SerializePayload where it writes itself).
template <class TPayload, auto cOpcode>
struct Relayed final
    : std::conditional_t<std::is_same_v<decltype(cOpcode), RequestOpcode>, RequestMessage, NotifyMessage>,
      TPayload
{
    using Message = std::conditional_t<std::is_same_v<decltype(cOpcode), RequestOpcode>, RequestMessage, NotifyMessage>;
    using Payload = TPayload;
    static constexpr decltype(cOpcode) Opcode = cOpcode;

    Relayed() : Message(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        if constexpr (requires { &Payload::SerializePayload; })
            Payload::SerializePayload(aWriter);
        else
            Wire::Write(aWriter, static_cast<const Payload&>(*this));
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        if constexpr (requires { &Payload::DeserializePayload; })
            Payload::DeserializePayload(aReader);
        else
            Wire::Read(aReader, static_cast<Payload&>(*this));
    }

    bool operator==(const Relayed& acRhs) const noexcept { return Payload::operator==(acRhs); }
};
