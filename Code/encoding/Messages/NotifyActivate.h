#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct NotifyActivate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActivate;
    NotifyActivate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyActivate& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id && ActivatorId == acRhs.ActivatorId && ActivatorRef == acRhs.ActivatorRef && PreActivationOpenState == acRhs.PreActivationOpenState; }

    GameId Id;
    uint32_t ActivatorId;
    GameId ActivatorRef{}; // a reference, not an actor, activated it (a script's Activate): ActivatorId is 0
    uint8_t PreActivationOpenState;
};
