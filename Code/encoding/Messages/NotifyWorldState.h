#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Rotator2_NetQuantize.h>

struct NotifyWorldState final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyWorldState;

    NotifyWorldState()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    struct EntityEntry
    {
        uint32_t SessionId{};
        Vector3_NetQuantize Position{};
        Rotator2_NetQuantize Rotation{};
    };

    std::vector<EntityEntry> Entities;
};
