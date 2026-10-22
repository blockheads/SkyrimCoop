#pragma once

#include "Message.h"

#include <Structs/Inventory.h>

struct NotifyInventoryChanges final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyInventoryChanges;

    NotifyInventoryChanges()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyInventoryChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && SessionId == acRhs.SessionId && Item == acRhs.Item && Drop == acRhs.Drop; }

    uint32_t SessionId{};
    Inventory::Entry Item{};
    bool Drop = false;
};
