#pragma once

#include "Message.h"
#include <Structs/Inventory.h>

// The owner's full worn equipment for one actor, re-sent periodically so a copy that drifted
// (missed or undone change) converges back to the owner's state.
struct RequestEquipmentSnapshot final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestEquipmentSnapshot;

    RequestEquipmentSnapshot()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestEquipmentSnapshot() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestEquipmentSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Equipment == acRhs.Equipment; }

    uint32_t ServerId{};
    Inventory Equipment{};
};
