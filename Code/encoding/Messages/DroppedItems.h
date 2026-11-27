#pragma once

#include <Messages/Message.h>
#include <Messages/WorldState.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>

// Dropped items (docs/design/dropped-items.md): a guest's drop and pickups go through the host's game, which makes
// and deletes the one reference the party sees.

// A guest's player dropped an item: its game made Tag (its own reference's form id) with Item's count and extra data
// at Placement; the host's game makes the party's.
struct DropItemRequest final : MessageOf<DropItemRequest, kDropItemRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Tag, Item, Placement); }
    bool operator==(const DropItemRequest&) const noexcept = default;

    uint32_t Tag{};
    Inventory::Entry Item{};
    ObjectChange::Placement Placement{};
};

// To the dropper: the host's game made its drop Tag as Object (a created id), whose twin its own reference is; an empty
// Object: the host's game cannot make it (not in its game), it stays the dropper's own.
struct NotifyItemDropped final : MessageOf<NotifyItemDropped, kNotifyItemDropped>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Tag, Object); }
    bool operator==(const NotifyItemDropped&) const noexcept = default;

    uint32_t Tag{};
    GameId Object{};
};

// A guest's player picks up the party's item Object, or its own drop Tag the host has not answered yet.
struct PickUpRequest final : MessageOf<PickUpRequest, kPickUpRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Object, Tag); }
    bool operator==(const PickUpRequest&) const noexcept = default;

    GameId Object{};
    uint32_t Tag{};
};

// To the picker: it was first, the item is its own (Item: what the host's reference held); gone from the world.
struct NotifyPickUp final : MessageOf<NotifyPickUp, kNotifyPickUp>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Object, Tag, Item); }
    bool operator==(const NotifyPickUp&) const noexcept = default;

    GameId Object{};
    uint32_t Tag{};
    Inventory::Entry Item{};
};
