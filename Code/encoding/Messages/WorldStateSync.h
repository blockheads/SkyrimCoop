#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Rotator2_NetQuantize.h>

struct WorldStateSync final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kWorldStateSync;

    WorldStateSync()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    struct EntityEntry
    {
        uint32_t ServerId{};
        Vector3_NetQuantize Position{};
        Rotator2_NetQuantize Rotation{};
    };

    TiltedPhoques::Vector<EntityEntry> Entities;
};
