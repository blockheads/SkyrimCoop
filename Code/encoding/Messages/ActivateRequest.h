#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct ActivateRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kActivateRequest;

    ActivateRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ActivateRequest& acRhs) const noexcept { return Id == acRhs.Id && ActivatorId == acRhs.ActivatorId && CellId == acRhs.CellId && PreActivationOpenState == acRhs.PreActivationOpenState && GetOpcode() == acRhs.GetOpcode(); }

    GameId Id;
    GameId CellId;
    uint32_t ActivatorId;
    uint8_t PreActivationOpenState;
};
