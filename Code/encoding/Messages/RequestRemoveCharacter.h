#pragma once

#include "Message.h"

struct RequestRemoveCharacter final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestRemoveCharacter;

    RequestRemoveCharacter()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestRemoveCharacter() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestRemoveCharacter& achRhs) const noexcept { return ServerId == achRhs.ServerId && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t ServerId;
};
