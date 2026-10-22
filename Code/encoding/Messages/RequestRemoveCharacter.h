#pragma once

#include "Message.h"

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

    bool operator==(const RequestRemoveCharacter& achRhs) const noexcept { return SessionId == achRhs.SessionId && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t SessionId;
};
