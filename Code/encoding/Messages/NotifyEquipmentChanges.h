#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct NotifyEquipmentChanges final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyEquipmentChanges;

    NotifyEquipmentChanges()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyEquipmentChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && SessionId == acRhs.SessionId && ItemId == acRhs.ItemId && EquipSlotId == acRhs.EquipSlotId && Count == acRhs.Count && Unequip == acRhs.Unequip && IsSpell == acRhs.IsSpell && IsShout == acRhs.IsShout;
    }

    uint32_t SessionId{};
    GameId ItemId{};
    GameId EquipSlotId{};
    uint32_t Count{};
    bool Unequip = false;
    bool IsSpell = false;
    bool IsShout = false;
};
