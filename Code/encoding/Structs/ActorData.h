#pragma once

#include <Structs/ActorValues.h>
#include <Structs/Inventory.h>
#include <Wire/Buffer.h>

struct ActorData
{
    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorData& acRhs) const noexcept
    {
        return InitialActorValues == acRhs.InitialActorValues &&
               InitialInventory == acRhs.InitialInventory && IsDead == acRhs.IsDead &&
               IsWeaponDrawn == acRhs.IsWeaponDrawn;
    }

    ActorValues InitialActorValues{};
    Inventory InitialInventory{};
    bool IsDead{};
    bool IsWeaponDrawn{};
};
