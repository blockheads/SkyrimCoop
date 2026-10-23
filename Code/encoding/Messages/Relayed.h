#pragma once

#include "Message.h"
#include "Payloads.h"
#include "RelayedMessages.h"
#include <Wire/Buffer.h>

#include <type_traits>

// A request or notification carrying a shared payload unchanged: its bytes are the payload's.
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
        Payload::SerializePayload(aWriter);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        Payload::DeserializePayload(aReader);
    }

    bool operator==(const Relayed& acRhs) const noexcept
    {
        return this->GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs);
    }
};
