#pragma once

#include <Structs/GameId.h>
#include <Structs/LockData.h>
#include <Structs/Inventory.h>
#include <Structs/GridCellCoords.h>

struct ObjectData
{
    ObjectData() = default;
    ~ObjectData() = default;

    bool operator==(const ObjectData& acRhs) const noexcept;
    bool operator!=(const ObjectData& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    uint32_t SessionId{};
    GameId Id{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CurrentCoords{};
    LockData CurrentLockData{};
    Inventory CurrentInventory{};
    bool IsSenderFirst{};
};
