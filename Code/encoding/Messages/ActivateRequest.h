#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct ActivateRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kActivateRequest;

    ActivateRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ActivateRequest& acRhs) const noexcept { return Id == acRhs.Id && ActivatorId == acRhs.ActivatorId && ActivatorRef == acRhs.ActivatorRef && CellId == acRhs.CellId && PreActivationOpenState == acRhs.PreActivationOpenState && GetOpcode() == acRhs.GetOpcode(); }

    GameId Id;
    GameId CellId;
    uint32_t ActivatorId;
    GameId ActivatorRef{}; // a reference, not an actor, activated it (a script's Activate): ActivatorId is 0
    uint8_t PreActivationOpenState;
};
