#include <Structs/RecordPart.h>
#include <Wire/Serialization.h>

#include <algorithm>

using Wire::Serialization;

void RecordPart::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(Kind), 8);
    Serialization::WriteVarInt(aWriter, Version);
    Serialization::WriteString(aWriter, Bytes);
}

void RecordPart::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t kind = 0;
    aReader.ReadBits(kind, 8);
    Kind = static_cast<RecordKind>(kind & 0xFF);
    Version = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Bytes = Serialization::ReadString(aReader);
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
