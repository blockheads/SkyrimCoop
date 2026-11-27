#pragma once

#include "Message.h"

#include <Structs/Inventory.h>
#include <Wire/Buffer.h>

struct NotifyInventoryChanges final : MessageOf<NotifyInventoryChanges, kNotifyInventoryChanges>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(SessionId, Item, Drop); }
    bool operator==(const NotifyInventoryChanges&) const noexcept = default;

    uint32_t SessionId{};
    Inventory::Entry Item{};
    bool Drop = false;
};
