#pragma once

#include <Messages/Message.h>
#include <Messages/WorldState.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>

// Dropped items (docs/design/dropped-items.md): a guest's drop and pickups go through the host's game, which makes
// and deletes the one reference the party sees.

// A guest's player dropped an item: its game made Tag (its own reference's form id) with Item's count and extra data
// at Placement; the host's game makes the party's.
struct DropItemRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDropItemRequest;
    DropItemRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const DropItemRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Tag == acRhs.Tag && Item == acRhs.Item && Placement == acRhs.Placement;
    }

    uint32_t Tag{};
    Inventory::Entry Item{};
    ObjectChange::Placement Placement{};
};

// To the dropper: the host's game made its drop Tag as Object (a created id), whose twin its own reference is; an empty
// Object: the host's game cannot make it (not in its game), it stays the dropper's own.
struct NotifyItemDropped final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyItemDropped;
    NotifyItemDropped() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyItemDropped& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Tag == acRhs.Tag && Object == acRhs.Object;
    }

    uint32_t Tag{};
    GameId Object{};
};

// A guest's player picks up the party's item Object, or its own drop Tag the host has not answered yet.
struct PickUpRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kPickUpRequest;
    PickUpRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PickUpRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Object == acRhs.Object && Tag == acRhs.Tag;
    }

    GameId Object{};
    uint32_t Tag{};
};

// To the picker: it was first, the item is its own (Item: what the host's reference held); gone from the world.
struct NotifyPickUp final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPickUp;
    NotifyPickUp() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPickUp& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Object == acRhs.Object && Tag == acRhs.Tag && Item == acRhs.Item;
    }

    GameId Object{};
    uint32_t Tag{};
    Inventory::Entry Item{};
};
