#include <Structs/RecordPart.h>
#include <Structs/ItemsPart.h>
#include <Wire/Serialization.h>

#include <algorithm>

using Wire::Serialization;

void RecordPart::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(Kind), 8);
    Serialization::WriteVarInt(aWriter, Version);
    // Its own length, not WriteString's 16 bits: a full items part can pass 64 KB.
    Serialization::WriteVarInt(aWriter, Bytes.size());
    aWriter.WriteBytes(std::span(reinterpret_cast<const uint8_t*>(Bytes.data()), Bytes.size()));
}

void RecordPart::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t kind = 0;
    aReader.ReadBits(kind, 8);
    Kind = static_cast<RecordKind>(kind & 0xFF);
    Version = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Bytes.assign(Serialization::ReadVarInt(aReader) & 0xFFFFFF, '\0');
    aReader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(Bytes.data()), Bytes.size()));
}

void RecordParts::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Parts.size());
    for (const auto& cPart : Parts)
        cPart.Serialize(aWriter);
}

void RecordParts::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    // A count past what the bytes can hold is a broken message: nothing read.
    const uint64_t cCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), 0xFF);
    Parts.clear();
    for (uint64_t i = 0; i < cCount; ++i)
        Parts.emplace_back().Deserialize(aReader);
}

const RecordPart* RecordParts::Find(RecordKind aKind) const noexcept
{
    const auto it = std::ranges::find(Parts, aKind, &RecordPart::Kind);
    return it != Parts.end() ? &*it : nullptr;
}

std::optional<std::string> MergedPart(const RecordPart* apHeld, const RecordPart& acChange) noexcept
{
    if (acChange.Kind != RecordKind::kItems)
        return acChange.Bytes;
    ItemsPart change;
    if (!FromPartBytes(acChange.Bytes, change))
        return std::nullopt;
    if (change.Full)
        return acChange.Bytes;
    ItemsPart held;
    if (!apHeld || apHeld->Version + 1 != acChange.Version || !FromPartBytes(apHeld->Bytes, held))
        return std::nullopt;
    return ToPartBytes(ItemsPart::Merged(held, change));
}
