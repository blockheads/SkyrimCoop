#pragma once

#include "Message.h"
#include <Structs/ActorWhereabouts.h>
#include <Wire/Buffer.h>

struct NotifyRemoveCharacter final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyRemoveCharacter;

    NotifyRemoveCharacter()
        : NotifyMessage(Opcode)
    {
    }

    virtual ~NotifyRemoveCharacter() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRemoveCharacter& achRhs) const noexcept { return SessionId == achRhs.SessionId && Whereabouts == achRhs.Whereabouts && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t SessionId;
    // Where the owner's actor is now; its copies follow.
    ActorWhereabouts Whereabouts{};
};
