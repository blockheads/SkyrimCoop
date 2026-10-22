#pragma once

#include "Message.h"
#include <Structs/ActorWhereabouts.h>
#include <Wire/Buffer.h>

struct RequestRemoveCharacter final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestRemoveCharacter;

    RequestRemoveCharacter()
        : RequestMessage(Opcode)
    {
    }

    virtual ~RequestRemoveCharacter() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestRemoveCharacter& achRhs) const noexcept { return SessionId == achRhs.SessionId && Whereabouts == achRhs.Whereabouts && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t SessionId;
    // Where the owner's actor is now; its copies follow.
    ActorWhereabouts Whereabouts{};
};
