#pragma once

#include "Message.h"

struct NotifyRemoveCharacter final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyRemoveCharacter;

    NotifyRemoveCharacter()
        : NotifyMessage(Opcode)
    {
    }

    virtual ~NotifyRemoveCharacter() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRemoveCharacter& achRhs) const noexcept { return SessionId == achRhs.SessionId && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t SessionId;
};
