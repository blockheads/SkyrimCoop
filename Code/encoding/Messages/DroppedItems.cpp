#include <Messages/DroppedItems.h>

#include <Wire/Serialization.h>

using Wire::Serialization;

namespace
{
void WritePlacement(Wire::Buffer::Writer& aWriter, const ObjectChange::Placement& acPlacement) noexcept
{
    acPlacement.Cell.Serialize(aWriter);
    acPlacement.WorldSpace.Serialize(aWriter);
    for (const float cValue : acPlacement.Position)
        Serialization::WriteFloat(aWriter, cValue);
    for (const float cValue : acPlacement.Rotation)
        Serialization::WriteFloat(aWriter, cValue);
}

void ReadPlacement(Wire::Buffer::Reader& aReader, ObjectChange::Placement& aPlacement) noexcept
{
    aPlacement.Cell.Deserialize(aReader);
    aPlacement.WorldSpace.Deserialize(aReader);
    for (float& value : aPlacement.Position)
        value = Serialization::ReadFloat(aReader);
    for (float& value : aPlacement.Rotation)
        value = Serialization::ReadFloat(aReader);
}
} // namespace

void DropItemRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tag);
    Item.Serialize(aWriter);
    WritePlacement(aWriter, Placement);
}

void DropItemRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Tag = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Item.Deserialize(aReader);
    ReadPlacement(aReader, Placement);
}

void NotifyItemDropped::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tag);
    Object.Serialize(aWriter);
}

void NotifyItemDropped::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Tag = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Object.Deserialize(aReader);
}

void PickUpRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Object.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Tag);
}

void PickUpRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Object.Deserialize(aReader);
    Tag = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
}

void NotifyPickUp::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Object.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Tag);
    Item.Serialize(aWriter);
}

void NotifyPickUp::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Object.Deserialize(aReader);
    Tag = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Item.Deserialize(aReader);
}
