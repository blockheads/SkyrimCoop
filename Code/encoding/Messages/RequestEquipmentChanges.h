#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Wire/Buffer.h>

struct RequestEquipmentChanges final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestEquipmentChanges;

    RequestEquipmentChanges()
        : RequestMessage(Opcode)
    {
    }

    virtual ~RequestEquipmentChanges() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestEquipmentChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && SessionId == acRhs.SessionId && ItemId == acRhs.ItemId && EquipSlotId == acRhs.EquipSlotId && Count == acRhs.Count && Unequip == acRhs.Unequip && IsSpell == acRhs.IsSpell && IsShout == acRhs.IsShout && IsAmmo == acRhs.IsAmmo &&
               CurrentInventory == acRhs.CurrentInventory;
    }

    uint32_t SessionId{};
    GameId ItemId{};
    GameId EquipSlotId{};
    uint32_t Count{};
    bool Unequip = false;
    bool IsSpell = false;
    bool IsShout = false;
    bool IsAmmo = false;

    Inventory CurrentInventory{};
};
