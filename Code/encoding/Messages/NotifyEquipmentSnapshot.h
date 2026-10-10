#pragma once

#include "Message.h"
#include <Structs/Inventory.h>

// The owner's full worn equipment for one actor, re-sent periodically so a copy that drifted
// (missed or undone change) converges back to the owner's state.
struct NotifyEquipmentSnapshot final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyEquipmentSnapshot;

    NotifyEquipmentSnapshot()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyEquipmentSnapshot() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyEquipmentSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Equipment == acRhs.Equipment; }

    uint32_t ServerId{};
    Inventory Equipment{};
};
