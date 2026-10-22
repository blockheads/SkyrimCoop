#pragma once

#include "Message.h"
#include <Structs/ActorData.h>

struct NotifySpawnData final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifySpawnData;

    NotifySpawnData()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifySpawnData& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id && NewActorData == acRhs.NewActorData;
    }

    uint32_t Id{};
    ActorData NewActorData{};
};
